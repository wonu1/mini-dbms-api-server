# Agent C - Concurrency And Bench Client

## Mission
- Own the request queue and worker thread pool.
- Own queue full, close, and stop behavior.
- Own the C benchmark client used for worker-count comparison.

## Owned Files
- `api_server/include/job_queue.h`
- `api_server/include/thread_pool.h`
- `api_server/src/concurrency/job_queue.c`
- `api_server/src/concurrency/thread_pool.c`
- `api_server/tests/test_job_queue.c`
- `bench/client/bench_client.h`
- `bench/client/bench_client.c`
- `bench/client/main.c`

## Owned Functions
- `job_queue_init`
- `job_queue_push`
- `job_queue_pop`
- `job_queue_close`
- `job_queue_destroy`
- `job_queue_size`
- `job_queue_is_closed`
- `thread_pool_init`
- `thread_pool_start`
- `thread_pool_stop`
- `thread_pool_destroy`
- `thread_pool_is_started`
- `bench_config_set_defaults`
- `bench_config_parse_args`
- `bench_scenario_name`
- `bench_run`
- Bench client `main`

## What You Build
- Queue storage and synchronization
- Worker lifecycle and shutdown flow
- Queue full handling
- Benchmark client for `workers=1` vs `workers=4`
- Select and insert benchmark scenario runners

## What You Must Not Touch
- `api_server/src/server/*`
- `api_server/src/http/*`
- `db_engine/src/runtime/*`
- `bench/scripts/*`
- Any frozen engine-core path listed in `TEAM_SPLIT.md`

## Read-Only Reference Files
- `api_server/include/api_types.h`
- `api_server/include/server_app.h`
- `api_server/include/http_request.h`
- `api_server/include/http_response.h`
- `db_engine/include/engine_api.h`
- `db_engine/include/engine_types.h`

## Done Criteria
- Queue and thread pool logic stay inside the concurrency folder.
- Benchmark client logic stays inside `bench/client/*`.
- No server-core, HTTP JSON, or engine runtime internals are implemented in your files.
- You do not edit files owned by others.

## If Blocked
- If `QueryJob` needs more fields, do not change `api_types.h` alone; ask Agent B and the team.
- If the engine call flow needs more hooks, request a public API update from Agent D.
