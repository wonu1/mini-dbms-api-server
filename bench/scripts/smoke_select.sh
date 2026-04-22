#!/usr/bin/env sh
set -eu

# SELECT smoke test 스크립트다.
# 실행 중인 API 서버에 /health와 /query SELECT 요청을 보내
# 서버가 기본 요청을 처리할 수 있는지 빠르게 확인한다.

HOST="${API_HOST:-127.0.0.1}"
PORT="${API_PORT:-8080}"
TABLE="${API_SMOKE_TABLE:-users}"
BASE="http://${HOST}:${PORT}"

echo "-- smoke_select: GET ${BASE}/health"
curl -sS -f "${BASE}/health"
echo

echo "-- smoke_select: POST ${BASE}/query"
curl -sS -f -X POST "${BASE}/query" \
    -H "Content-Type: application/json" \
    --data "{\"sql\":\"SELECT * FROM ${TABLE}\",\"request_id\":\"smoke-select-1\"}"
echo
