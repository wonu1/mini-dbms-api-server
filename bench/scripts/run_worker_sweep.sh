#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

RUN_ID="${SWEEP_RUN_ID:-$(date '+%Y%m%d-%H%M%S')}"
RESULTS_ROOT="${SWEEP_RESULTS_ROOT:-$REPO_ROOT/bench/results}"
RESULTS_DIR="$RESULTS_ROOT/worker-sweep-$RUN_ID"
RAW_DIR="$RESULTS_DIR/raw"

PORT="${SWEEP_PORT:-18080}"
QUEUE_CAPACITY="${SWEEP_QUEUE_CAPACITY:-256}"
CLIENT_WORKERS="${SWEEP_CLIENT_WORKERS:-32}"
REQUESTS="${SWEEP_REQUESTS:-80000}"
REPEAT="${SWEEP_REPEAT:-3}"
WORKERS_START="${SWEEP_WORKERS_START:-1}"
WORKERS_END="${SWEEP_WORKERS_END:-30}"
BENCH_TIMEOUT_SEC="${SWEEP_BENCH_TIMEOUT_SEC:-900}"
KEEP_TMP="${SWEEP_KEEP_TMP:-0}"
SCENARIOS="${SWEEP_SCENARIOS:-select select_range_1000}"

SERVER_PID=""
TMP_ROOT=""
TMP_REPO=""
BASELINE_USERS=""

require_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "missing required command: $1" >&2
        exit 1
    fi
}

stop_server() {
    if [[ -n "${SERVER_PID:-}" ]] && kill -0 "$SERVER_PID" >/dev/null 2>&1; then
        kill "$SERVER_PID" >/dev/null 2>&1 || true
        wait "$SERVER_PID" >/dev/null 2>&1 || true
    fi
    SERVER_PID=""
}

cleanup() {
    stop_server
    if [[ -n "${TMP_ROOT:-}" ]] && [[ -d "$TMP_ROOT" ]] && [[ "$KEEP_TMP" != "1" ]]; then
        rm -rf "$TMP_ROOT"
    fi
}

wait_for_health() {
    local attempts=0
    while (( attempts < 100 )); do
        if curl -fsS "http://127.0.0.1:${PORT}/health" >/dev/null 2>&1; then
            return 0
        fi
        attempts=$((attempts + 1))
        sleep 0.2
    done

    echo "server health check timed out on port ${PORT}" >&2
    return 1
}

json_payload_for_sql() {
    python3 - "$1" <<'PY'
import json
import sys

print(json.dumps({"sql": sys.argv[1]}))
PY
}

post_query() {
    local sql="$1"
    local payload

    payload="$(json_payload_for_sql "$sql")"
    curl -fsS -X POST "http://127.0.0.1:${PORT}/query" \
        -H "Content-Type: application/json" \
        --data "$payload"
}

require_response_fragment() {
    local body="$1"
    local fragment="$2"

    if [[ "$body" != *"$fragment"* ]]; then
        echo "response body missing fragment: $fragment" >&2
        echo "$body" >&2
        return 1
    fi
}

start_server() {
    local workers="$1"
    local server_log="$2"

    stop_server
    (
        cd "$TMP_REPO"
        exec ./build/api_server_stub \
            --port "$PORT" \
            --workers "$workers" \
            --queue-capacity "$QUEUE_CAPACITY"
    ) >"$server_log" 2>&1 &
    SERVER_PID=$!
    wait_for_health
}

validate_select_path() {
    local out_file="$1"
    local body

    body="$(post_query "SELECT * FROM users WHERE id = 1;")"
    printf '%s\n' "$body" >"$out_file"
    require_response_fragment "$body" '"status":"ok"'
    require_response_fragment "$body" '"rows":['
    require_response_fragment "$body" '"row_count":1'
}

validate_select_range_path() {
    local out_file="$1"
    local body

    body="$(post_query "SELECT * FROM users WHERE id BETWEEN 1 AND 1000;")"
    printf '%s\n' "$body" >"$out_file"
    require_response_fragment "$body" '"status":"ok"'
    require_response_fragment "$body" '"rows":['
    require_response_fragment "$body" '"row_count":1000'
}

validate_insert_path() {
    local out_file="$1"
    local sql
    local body

    sql="INSERT INTO users (name, age, email) VALUES ('worker_sweep_probe', 30, 'worker_sweep_probe_${RUN_ID}@example.com');"
    body="$(post_query "$sql")"
    printf '%s\n' "$body" >"$out_file"
    require_response_fragment "$body" '"status":"ok"'
    require_response_fragment "$body" '"affected_rows":1'
}

scenario_expected_rows() {
    case "$1" in
        select)
            echo 1
            ;;
        select_range_1000)
            echo 1000
            ;;
        insert)
            echo 1
            ;;
        *)
            echo 0
            ;;
    esac
}

validate_scenario_path() {
    local scenario="$1"
    local out_file="$2"

    case "$scenario" in
        select)
            validate_select_path "$out_file"
            ;;
        select_range_1000)
            validate_select_range_path "$out_file"
            ;;
        insert)
            validate_insert_path "$out_file"
            ;;
        *)
            echo "unsupported validation scenario: $scenario" >&2
            return 1
            ;;
    esac
}

parse_bench_output() {
    python3 - "$1" <<'PY'
import re
import sys

line = sys.argv[1]
match = re.search(
    r"scenario=(\w+) workers=(\d+) requests=(\d+) success=(\d+) failure=(\d+) elapsed=([0-9.]+)s req_per_sec=([0-9.]+)",
    line,
)
if not match:
    raise SystemExit(1)

scenario, client_workers, requests, success, failure, elapsed, attempted = match.groups()
requests = int(requests)
success = int(success)
failure = int(failure)
elapsed = float(elapsed)
attempted = float(attempted)
success_per_sec = (success / elapsed) if elapsed > 0 else 0.0
failure_rate_pct = (failure * 100.0 / requests) if requests > 0 else 0.0

print(
    ",".join(
        [
            scenario,
            client_workers,
            str(requests),
            str(success),
            str(failure),
            f"{elapsed:.6f}",
            f"{attempted:.2f}",
            f"{success_per_sec:.2f}",
            f"{failure_rate_pct:.4f}",
        ]
    )
)
PY
}

build_summary_csv() {
    local runs_csv="$1"
    local medians_csv="$2"
    local summary_txt="$3"

    python3 - "$runs_csv" "$medians_csv" "$summary_txt" <<'PY'
import csv
import statistics
import sys
from collections import defaultdict

runs_csv, medians_csv, summary_txt = sys.argv[1:4]

groups = defaultdict(list)
with open(runs_csv, "r", encoding="utf-8", newline="") as handle:
    reader = csv.DictReader(handle)
    for row in reader:
        key = (row["scenario"], int(row["server_workers"]))
        groups[key].append(row)

median_rows = []
best_by_scenario = {}
for (scenario, server_workers), rows in sorted(groups.items()):
    success_per_sec = [float(row["success_per_sec"]) for row in rows]
    elapsed_sec = [float(row["elapsed_sec"]) for row in rows]
    failures = [int(row["failure"]) for row in rows]
    attempted = [float(row["attempted_req_per_sec"]) for row in rows]
    rows_per_sec = [float(row["estimated_rows_per_sec"]) for row in rows]

    record = {
        "scenario": scenario,
        "server_workers": server_workers,
        "repeat_count": len(rows),
        "median_success_per_sec": f"{statistics.median(success_per_sec):.2f}",
        "min_success_per_sec": f"{min(success_per_sec):.2f}",
        "max_success_per_sec": f"{max(success_per_sec):.2f}",
        "median_elapsed_sec": f"{statistics.median(elapsed_sec):.6f}",
        "median_attempted_req_per_sec": f"{statistics.median(attempted):.2f}",
        "median_estimated_rows_per_sec": f"{statistics.median(rows_per_sec):.2f}",
        "min_estimated_rows_per_sec": f"{min(rows_per_sec):.2f}",
        "max_estimated_rows_per_sec": f"{max(rows_per_sec):.2f}",
        "max_failure": str(max(failures)),
        "total_failures": str(sum(failures)),
    }
    median_rows.append(record)

    current_best = best_by_scenario.get(scenario)
    current_score = float(record["median_success_per_sec"])
    if current_best is None or current_score > float(current_best["median_success_per_sec"]):
        best_by_scenario[scenario] = record

with open(medians_csv, "w", encoding="utf-8", newline="") as handle:
    writer = csv.DictWriter(
        handle,
        fieldnames=[
            "scenario",
            "server_workers",
            "repeat_count",
            "median_success_per_sec",
            "min_success_per_sec",
            "max_success_per_sec",
            "median_elapsed_sec",
            "median_attempted_req_per_sec",
            "median_estimated_rows_per_sec",
            "min_estimated_rows_per_sec",
            "max_estimated_rows_per_sec",
            "max_failure",
            "total_failures",
        ],
    )
    writer.writeheader()
    writer.writerows(median_rows)

with open(summary_txt, "w", encoding="utf-8") as handle:
    for scenario in sorted(best_by_scenario):
        best = best_by_scenario[scenario]
        handle.write(
            f"{scenario}: best_worker={best['server_workers']}, "
            f"median_success_per_sec={best['median_success_per_sec']}, "
            f"median_estimated_rows_per_sec={best['median_estimated_rows_per_sec']}, "
            f"max_failure={best['max_failure']}\n"
        )
PY
}

run_case() {
    local scenario="$1"
    local server_workers="$2"
    local repeat_index="$3"
    local server_log="$RAW_DIR/server-${scenario}-w$(printf '%03d' "$server_workers")-r$(printf '%02d' "$repeat_index").log"
    local bench_log="$RAW_DIR/bench-${scenario}-w$(printf '%03d' "$server_workers")-r$(printf '%02d' "$repeat_index").txt"
    local case_started_at
    local case_finished_at
    local bench_output
    local bench_exit_code
    local parsed
    local parsed_scenario
    local parsed_client_workers
    local parsed_requests
    local success
    local failure
    local elapsed_sec
    local attempted_req_per_sec
    local success_per_sec
    local failure_rate_pct
    local expected_rows_per_query
    local estimated_rows_per_sec

    if [[ "$scenario" == "insert" ]]; then
        cp "$BASELINE_USERS" "$TMP_REPO/db_engine/data/users.dat"
    fi

    case_started_at="$(date --iso-8601=seconds)"
    start_server "$server_workers" "$server_log"

    set +e
    bench_output="$(
        cd "$TMP_REPO" && \
        timeout "$BENCH_TIMEOUT_SEC" ./build/bench_client \
            --host 127.0.0.1 \
            --port "$PORT" \
            --workers "$CLIENT_WORKERS" \
            --requests "$REQUESTS" \
            --scenario "$scenario" 2>&1
    )"
    bench_exit_code=$?
    set -e

    printf '%s\n' "$bench_output" >"$bench_log"
    case_finished_at="$(date --iso-8601=seconds)"
    stop_server

    parsed="$(parse_bench_output "$bench_output")"
    IFS=, read -r \
        parsed_scenario \
        parsed_client_workers \
        parsed_requests \
        success \
        failure \
        elapsed_sec \
        attempted_req_per_sec \
        success_per_sec \
        failure_rate_pct <<<"$parsed"

    expected_rows_per_query="$(scenario_expected_rows "$scenario")"
    estimated_rows_per_sec="$(python3 - "$success_per_sec" "$expected_rows_per_query" <<'PY'
import sys

success_per_sec = float(sys.argv[1])
expected_rows = int(sys.argv[2])
print(f"{success_per_sec * expected_rows:.2f}")
PY
)"

    printf '%s\n' \
        "${RUN_ID},${GIT_BRANCH},${GIT_COMMIT},${scenario},${server_workers},${repeat_index},${QUEUE_CAPACITY},${CLIENT_WORKERS},${REQUESTS},${expected_rows_per_query},${success},${failure},${elapsed_sec},${attempted_req_per_sec},${success_per_sec},${estimated_rows_per_sec},${failure_rate_pct},${bench_exit_code},${case_started_at},${case_finished_at},${server_log},${bench_log}" \
        >>"$RUNS_CSV"

    echo "[${scenario}] worker=${server_workers} repeat=${repeat_index} success=${success} failure=${failure} success_per_sec=${success_per_sec} estimated_rows_per_sec=${estimated_rows_per_sec}"
}

trap cleanup EXIT

for cmd in git make curl python3 timeout cp rm mktemp; do
    require_cmd "$cmd"
done

if ! [[ "$PORT" =~ ^[0-9]+$ && "$QUEUE_CAPACITY" =~ ^[0-9]+$ && "$CLIENT_WORKERS" =~ ^[0-9]+$ && "$REQUESTS" =~ ^[0-9]+$ && "$REPEAT" =~ ^[0-9]+$ && "$WORKERS_START" =~ ^[0-9]+$ && "$WORKERS_END" =~ ^[0-9]+$ ]]; then
    echo "all numeric sweep parameters must be positive integers" >&2
    exit 1
fi

if (( PORT <= 0 || QUEUE_CAPACITY <= 0 || CLIENT_WORKERS <= 0 || REQUESTS <= 0 || REPEAT <= 0 || WORKERS_START <= 0 || WORKERS_END < WORKERS_START )); then
    echo "invalid sweep parameter range" >&2
    exit 1
fi

mkdir -p "$RAW_DIR"

GIT_BRANCH="$(git -C "$REPO_ROOT" branch --show-current)"
GIT_COMMIT="$(git -C "$REPO_ROOT" rev-parse --short HEAD)"
RUNS_CSV="$RESULTS_DIR/worker_sweep_runs.csv"
MEDIANS_CSV="$RESULTS_DIR/worker_sweep_medians.csv"
METADATA_JSON="$RESULTS_DIR/metadata.json"
SUMMARY_TXT="$RESULTS_DIR/run_summary.txt"

printf '%s\n' \
    "run_id,git_branch,git_commit,scenario,server_workers,repeat_index,server_queue_capacity,client_workers,requests,expected_rows_per_query,success,failure,elapsed_sec,attempted_req_per_sec,success_per_sec,estimated_rows_per_sec,failure_rate_pct,bench_exit_code,case_started_at,case_finished_at,server_log_path,bench_log_path" \
    >"$RUNS_CSV"

TMP_ROOT="$(mktemp -d /tmp/mini-dbms-worker-sweep-XXXXXX)"
TMP_REPO="$TMP_ROOT/repo"
mkdir -p "$TMP_REPO"
cp -a "$REPO_ROOT"/. "$TMP_REPO"/
rm -rf "$TMP_REPO/build" "$TMP_REPO/bench/results" "$TMP_REPO/.git" "$TMP_REPO/.python-libs" "$TMP_REPO/.venv-plot"

make -C "$TMP_REPO" clean >/dev/null 2>&1 || true
make -C "$TMP_REPO" all >"$RESULTS_DIR/build.log" 2>&1

BASELINE_USERS="$TMP_ROOT/users.dat.base"
cp "$TMP_REPO/db_engine/data/users.dat" "$BASELINE_USERS"

read -r -a SCENARIO_LIST <<<"$SCENARIOS"
for scenario in "${SCENARIO_LIST[@]}"; do
    if [[ "$scenario" != "select" && "$scenario" != "select_range_1000" && "$scenario" != "insert" ]]; then
        echo "unsupported scenario in SWEEP_SCENARIOS: $scenario" >&2
        exit 1
    fi
done

VALIDATION_SERVER_LOG="$RAW_DIR/server-validation.log"
for scenario in "${SCENARIO_LIST[@]}"; do
    if [[ "$scenario" == "insert" ]]; then
        cp "$BASELINE_USERS" "$TMP_REPO/db_engine/data/users.dat"
    fi
    start_server 4 "$VALIDATION_SERVER_LOG"
    validate_scenario_path "$scenario" "$RESULTS_DIR/validation_${scenario}_response.json"
    stop_server
done
cp "$BASELINE_USERS" "$TMP_REPO/db_engine/data/users.dat"

STARTED_AT="$(date --iso-8601=seconds)"
for scenario in "${SCENARIO_LIST[@]}"; do
    for (( server_workers = WORKERS_START; server_workers <= WORKERS_END; server_workers++ )); do
        for (( repeat_index = 1; repeat_index <= REPEAT; repeat_index++ )); do
            run_case "$scenario" "$server_workers" "$repeat_index"
        done
    done
done
FINISHED_AT="$(date --iso-8601=seconds)"

build_summary_csv "$RUNS_CSV" "$MEDIANS_CSV" "$SUMMARY_TXT"

python3 - "$METADATA_JSON" "${RESULTS_DIR}" "${RUNS_CSV}" "${MEDIANS_CSV}" ${BENCH_TIMEOUT_SEC} ${PORT} ${QUEUE_CAPACITY} ${CLIENT_WORKERS} ${REQUESTS} ${REPEAT} ${WORKERS_START} ${WORKERS_END} "${SCENARIOS}" "${KEEP_TMP}" "${TMP_REPO}" "${RUN_ID}" "${GIT_BRANCH}" "${GIT_COMMIT}" "${STARTED_AT}" "${FINISHED_AT}" "${REPO_ROOT}" "${RAW_DIR}" <<'PY'
import json
import sys

(
    metadata_path,
    results_dir,
    runs_csv,
    medians_csv,
    bench_timeout_sec,
    port,
    queue_capacity,
    client_workers,
    requests,
    repeat,
    workers_start,
    workers_end,
    scenarios_text,
    keep_tmp,
    temporary_repo,
    run_id,
    git_branch,
    git_commit,
    started_at,
    finished_at,
    repo_root,
    raw_dir,
) = sys.argv[1:]

scenarios = scenarios_text.split()
metadata = {
    "run_id": run_id,
    "git_branch": git_branch,
    "git_commit": git_commit,
    "started_at": started_at,
    "finished_at": finished_at,
    "repo_root": repo_root,
    "results_dir": results_dir,
    "raw_dir": raw_dir,
    "temporary_repo": temporary_repo if keep_tmp == "1" else None,
    "port": int(port),
    "queue_capacity": int(queue_capacity),
    "client_workers": int(client_workers),
    "requests": int(requests),
    "repeat": int(repeat),
    "workers_start": int(workers_start),
    "workers_end": int(workers_end),
    "scenarios": scenarios,
    "bench_timeout_sec": int(bench_timeout_sec),
    "validation_files": {
        scenario: f"{results_dir}/validation_{scenario}_response.json"
        for scenario in scenarios
    },
    "csv_files": {
        "runs": runs_csv,
        "medians": medians_csv,
    },
}

with open(metadata_path, "w", encoding="utf-8") as handle:
    json.dump(metadata, handle, ensure_ascii=True, indent=2)
PY

echo "worker sweep completed"
echo "results_dir=${RESULTS_DIR}"
echo "runs_csv=${RUNS_CSV}"
echo "medians_csv=${MEDIANS_CSV}"
echo "metadata_json=${METADATA_JSON}"
