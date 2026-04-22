# Agent A - Server Core

## Mission
- Own the server entrypoint and wiring layer.
- Handle runtime config, bootstrap order, and server lifecycle.
- Use only public headers from other modules.

## Owned Files
- `Makefile`
- `api_server/include/server_app.h`
- `api_server/include/http_server.h`
- `api_server/src/server/main.c`
- `api_server/src/server/server_app.c`
- `api_server/src/server/http_server.c`
- `api_server/tests/test_server_app.c`
- `bench/scripts/smoke_select.sh`
- `bench/scripts/smoke_insert.sh`

## Owned Functions
- `server_config_set_defaults`
- `server_config_validate`
- `server_config_parse_args`
- `server_app_print_usage`
- `server_app_run`
- `http_server_run`
- `http_server_request_stop`
- `http_server_stop_requested`
- `main`

## What You Build
- Parse `--port`, `--workers`, and `--queue-capacity`
- Define bootstrap and shutdown order
- Wire engine runtime, thread pool, and HTTP server startup
- Keep `main.c` thin
- Maintain smoke scripts

## What You Must Not Touch
- `api_server/src/http/*`
- `api_server/src/concurrency/*`
- `db_engine/src/runtime/*`
- `api_server/include/api_types.h`
- `db_engine/include/engine_types.h`
- Any frozen engine-core path listed in `TEAM_SPLIT.md`

## Read-Only Reference Files
- `api_server/include/api_types.h`
- `api_server/include/http_request.h`
- `api_server/include/http_response.h`
- `api_server/include/job_queue.h`
- `api_server/include/thread_pool.h`
- `db_engine/include/engine_api.h`
- `db_engine/include/engine_runtime.h`

## Done Criteria
- Startup flow is fully defined inside your files.
- `server_app_run` calls only public functions from B, C, and D.
- No logic from HTTP JSON, queue internals, or engine runtime leaks into your files.
- You do not edit files owned by others.

## If Blocked
- If you need a new field in a shared contract file, stop and ask for team approval.
- If another module is not ready, keep a TODO at the call site and do not edit that module.
