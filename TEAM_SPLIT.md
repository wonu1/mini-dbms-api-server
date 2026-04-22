# Mini DBMS API Server Team Split

## Goal
- Reduce merge conflicts during parallel branch work.
- Keep the current interface contracts frozen.
- Let each person work inside a clear file boundary.

## Core Rules
- Edit only files you own.
- Treat other owners' files as read-only.
- Do not change shared contract headers without team approval.
- Do not modify the existing DB engine core in this round.

## Shared Contract Files
- `api_server/include/api_types.h`
- `db_engine/include/engine_types.h`
- `db_engine/include/engine_api.h`
- `db_engine/include/engine_runtime.h`

These files define public contracts.  
Field changes, enum changes, and function signature changes require team approval first.

## Frozen Zones
No one edits these paths in this round.

- `db_engine/include/interface.h`
- `db_engine/include/index_manager.h`
- `db_engine/include/bptree.h`
- `db_engine/src/input/*`
- `db_engine/src/parser/*`
- `db_engine/src/schema/*`
- `db_engine/src/executor/*`
- `db_engine/src/index/*`
- `db_engine/src/bptree/*`
- `db_engine/src/main.c`
- `db_engine/tests/test_bptree.c`
- `db_engine/tests/test_executor.c`
- `db_engine/tests/test_index.c`
- `db_engine/tests/test_parser.c`
- `db_engine/tests/test_schema.c`
- `db_engine/samples/*`
- `db_engine/data/*`

## Ownership

### Agent A - Server Core
- Suggested branch: `feature/A-server-core`
- Own files:
- `Makefile`
- `api_server/include/server_app.h`
- `api_server/include/http_server.h`
- `api_server/src/server/main.c`
- `api_server/src/server/server_app.c`
- `api_server/src/server/http_server.c`
- `api_server/tests/test_server_app.c`
- `bench/scripts/smoke_select.sh`
- `bench/scripts/smoke_insert.sh`

### Agent B - HTTP Contract
- Suggested branch: `feature/B-http-contract`
- Own files:
- `api_server/include/api_types.h`
- `api_server/include/http_request.h`
- `api_server/include/http_response.h`
- `api_server/src/http/http_request.c`
- `api_server/src/http/http_response.c`
- `api_server/tests/test_http_request.c`
- `api_server/tests/test_http_response.c`

### Agent C - Concurrency And Bench Client
- Suggested branch: `feature/C-concurrency`
- Own files:
- `api_server/include/job_queue.h`
- `api_server/include/thread_pool.h`
- `api_server/src/concurrency/job_queue.c`
- `api_server/src/concurrency/thread_pool.c`
- `api_server/tests/test_job_queue.c`
- `bench/client/bench_client.h`
- `bench/client/bench_client.c`
- `bench/client/main.c`

### Agent D - Engine Runtime Bridge
- Suggested branch: `feature/D-engine-runtime`
- Own files:
- `db_engine/include/engine_types.h`
- `db_engine/include/engine_api.h`
- `db_engine/include/engine_runtime.h`
- `db_engine/src/runtime/engine_api.c`
- `db_engine/src/runtime/engine_runtime.c`
- `db_engine/tests/test_engine_runtime.c`

## Recommended Merge Order
- First wave: Agent D, Agent B, Agent C
- Last wave: Agent A

Agent A owns the startup and final wiring layer, so merging A last should reduce conflicts.

## Hard No Rules
- Do not edit another person's `.c` file.
- Do not edit another person's test file.
- Do not edit headers you do not own.
- Do not edit frozen zones.
- Do not change shared contract files alone.

## Exception Rule
- If a contract change is truly required, stop and discuss first.
- After approval, only the file owner makes the change.
- Everyone else rebases after that change lands.

## Per-Agent Docs
- `agents/AGENT_A_SERVER_CORE.md`
- `agents/AGENT_B_HTTP.md`
- `agents/AGENT_C_CONCURRENCY.md`
- `agents/AGENT_D_ENGINE_RUNTIME.md`
