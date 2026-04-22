#!/usr/bin/env sh
set -eu

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
