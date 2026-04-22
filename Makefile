CC ?= gcc
CFLAGS ?= -std=c99 -Wall -Wextra -Iapi_server/include -Idb_engine/include -Ibench/client

BUILD_DIR := build

API_SERVER_COMMON_SRCS := \
	api_server/src/server/server_app.c \
	api_server/src/server/http_server.c \
	api_server/src/http/http_request.c \
	api_server/src/http/http_response.c \
	api_server/src/concurrency/job_queue.c \
	api_server/src/concurrency/thread_pool.c \
	db_engine/src/runtime/engine_api.c \
	db_engine/src/runtime/engine_runtime.c

API_SERVER_BIN := $(BUILD_DIR)/api_server_stub
BENCH_CLIENT_BIN := $(BUILD_DIR)/bench_client

TEST_BINS := \
	$(BUILD_DIR)/test_http_request \
	$(BUILD_DIR)/test_http_response \
	$(BUILD_DIR)/test_job_queue \
	$(BUILD_DIR)/test_server_app \
	$(BUILD_DIR)/test_engine_runtime

all: $(API_SERVER_BIN) $(BENCH_CLIENT_BIN)

$(BUILD_DIR):
	mkdir -p $@

$(API_SERVER_BIN): api_server/src/server/main.c $(API_SERVER_COMMON_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^

$(BENCH_CLIENT_BIN): bench/client/main.c bench/client/bench_client.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD_DIR)/test_http_request: api_server/tests/test_http_request.c api_server/src/http/http_request.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD_DIR)/test_http_response: api_server/tests/test_http_response.c api_server/src/http/http_response.c db_engine/src/runtime/engine_api.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD_DIR)/test_job_queue: api_server/tests/test_job_queue.c api_server/src/concurrency/job_queue.c api_server/src/http/http_request.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD_DIR)/test_server_app: api_server/tests/test_server_app.c $(API_SERVER_COMMON_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD_DIR)/test_engine_runtime: db_engine/tests/test_engine_runtime.c db_engine/src/runtime/engine_api.c db_engine/src/runtime/engine_runtime.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^

test: $(TEST_BINS)
	@$(BUILD_DIR)/test_http_request
	@$(BUILD_DIR)/test_http_response
	@$(BUILD_DIR)/test_job_queue
	@$(BUILD_DIR)/test_server_app
	@$(BUILD_DIR)/test_engine_runtime

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all test clean
