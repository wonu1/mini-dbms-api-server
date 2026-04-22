#!/usr/bin/env sh
set -eu

# INSERT smoke test 스크립트다.
# 실행 중인 API 서버에 /health와 /query INSERT 요청을 보내
# 쓰기 요청 경로가 최소한 동작하는지 확인한다.

HOST="${API_HOST:-127.0.0.1}"
PORT="${API_PORT:-8080}"
TABLE="${API_SMOKE_TABLE:-users}"
BASE="http://${HOST}:${PORT}"

echo "-- smoke_insert: GET ${BASE}/health"
curl -sS -f "${BASE}/health"
echo

echo "-- smoke_insert: POST ${BASE}/query"
curl -sS -f -X POST "${BASE}/query" \
    -H "Content-Type: application/json" \
    --data "{\"sql\":\"INSERT INTO ${TABLE} (name, age) VALUES ('smoke', 30)\",\"request_id\":\"smoke-insert-1\"}"
echo
