# Agent B - HTTP Contract

## Mission
- Own the request and response contract.
- Implement JSON parsing and JSON serialization.
- Keep all HTTP payload rules inside the HTTP module.

## Owned Files
- `api_server/include/api_types.h`
- `api_server/include/http_request.h`
- `api_server/include/http_response.h`
- `api_server/src/http/http_request.c`
- `api_server/src/http/http_response.c`
- `api_server/tests/test_http_request.c`
- `api_server/tests/test_http_response.c`

## Owned Functions
- `api_query_request_init`
- `api_query_request_free`
- `query_job_init`
- `query_job_free`
- `http_query_is_single_statement`
- `http_parse_query_request`
- `http_response_init`
- `http_response_free`
- `http_build_health_response`
- `http_build_query_success_response`
- `http_build_error_response`
- `http_status_from_engine_error`
- `http_error_code_from_engine_error`

## What You Build
- Enforce `Content-Type: application/json`
- Parse `sql` and optional `request_id`
- Ignore unknown JSON fields
- Enforce one SQL statement per request
- Build success and error JSON payloads
- Echo `request_id` back in the response when present

## What You Must Not Touch
- `api_server/src/server/*`
- `api_server/src/concurrency/*`
- `db_engine/src/runtime/*`
- `Makefile`
- Any frozen engine-core path listed in `TEAM_SPLIT.md`

## Contract Notes
- `api_server/include/api_types.h` is owned by you, but it is still a shared contract file.
- Do not change `ServerConfig`, `ApiQueryRequest`, or `QueryJob` without team approval.
- `db_engine/include/engine_types.h` is read-only for you.

## Read-Only Reference Files
- `api_server/include/server_app.h`
- `api_server/include/http_server.h`
- `api_server/include/job_queue.h`
- `api_server/include/thread_pool.h`
- `db_engine/include/engine_api.h`
- `db_engine/include/engine_types.h`

## Done Criteria
- Request parsing and response serialization are fully contained in your files.
- No JSON logic leaks into server-core or concurrency files.
- Wire format is clear from your headers and tests alone.
- You do not edit files owned by others.

## If Blocked
- If you need more fields from the engine result, ask Agent D for a contract change.
- If you need a new request field, do not add it alone; get team approval first.
