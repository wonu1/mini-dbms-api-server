# Agent D - Engine Runtime Bridge

## Mission
- Own the server-facing runtime bridge inside `db_engine`.
- Own runtime init, preloading, global RW-lock policy, and execute boundary.
- Keep the old engine core read-only in this round.

## Suggested Branch
- `feature/D-engine-runtime`

## Owned Files
- `db_engine/include/engine_types.h`
- `db_engine/include/engine_api.h`
- `db_engine/include/engine_runtime.h`
- `db_engine/src/runtime/engine_api.c`
- `db_engine/src/runtime/engine_runtime.c`
- `db_engine/tests/test_engine_runtime.c`

## Owned Functions
- `engine_runtime_get_state`
- `engine_runtime_set_schema_dir`
- `engine_runtime_default_schema_dir`
- `engine_runtime_init`
- `engine_runtime_prepare_all`
- `engine_runtime_shutdown`
- `engine_execute_sql`
- `engine_response_free`
- `engine_error_code_name`

## What You Build
- The server-facing engine entrypoint
- Schema scan and preload flow
- Global RW-lock ownership and lock timing
- Conversion from old engine outputs to `EngineResponse`
- `SELECT` and `INSERT` execution boundary

## What You Must Not Touch
- `db_engine/src/input/*`
- `db_engine/src/parser/*`
- `db_engine/src/schema/*`
- `db_engine/src/executor/*`
- `db_engine/src/index/*`
- `db_engine/src/bptree/*`
- `api_server/*`
- `bench/*`

## Contract Notes
- `db_engine/include/engine_types.h` is owned by you, but it is still a shared contract file.
- Do not change `EngineResponse`, `EngineErrorCode`, or `EngineRuntimeState` without team approval.
- `db_engine/include/interface.h` is read-only for you.

## Read-Only Reference Files
- `db_engine/include/interface.h`
- `db_engine/include/index_manager.h`
- `api_server/include/http_response.h`
- `api_server/include/api_types.h`

## Done Criteria
- The server can call `engine_execute_sql()` without touching old engine internals directly.
- Runtime init and shutdown rules are fully contained in your files.
- Old engine-core files remain untouched.
- You do not edit files owned by others.

## If Blocked
- If a change in the old engine core seems necessary, stop and discuss it first.
- If the HTTP output needs different engine fields, align with Agent B before changing contracts.
