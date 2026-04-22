CC ?= gcc
CFLAGS ?= -std=c99 -Wall -Wextra -Iapi_server/include -Idb_engine/include -Ibench/client

# 루트 Makefile은 API 서버, 벤치 클라이언트, 통합 단위 테스트를 한 번에 빌드한다.
# 팀원이 루트에서 make / make test만 실행해도 현재 프로젝트 상태를 확인할 수 있게 한다.

BUILD_DIR := build

# API 서버가 사용하는 엔진 런타임 bridge 소스 목록이다.
DB_ENGINE_RUNTIME_SRCS := \
	db_engine/src/runtime/engine_api.c \
	db_engine/src/runtime/engine_runtime.c

# 기존 DB 엔진 core 소스 목록이다.
DB_ENGINE_CORE_SRCS := \
	db_engine/src/input/lexer.c \
	db_engine/src/parser/parser.c \
	db_engine/src/schema/schema.c \
	db_engine/src/executor/executor.c \
	db_engine/src/bptree/bptree.c \
	db_engine/src/index/index_manager.c

# API 서버 실행 파일과 server_app 테스트가 함께 링크해야 하는 공통 소스다.
API_SERVER_COMMON_SRCS := \
	api_server/src/server/server_app.c \
	api_server/src/server/http_server.c \
	api_server/src/http/http_request.c \
	api_server/src/http/http_response.c \
	api_server/src/concurrency/job_queue.c \
	api_server/src/concurrency/thread_pool.c \
	$(DB_ENGINE_RUNTIME_SRCS) \
	$(DB_ENGINE_CORE_SRCS)

API_SERVER_BIN := $(BUILD_DIR)/api_server_stub
BENCH_CLIENT_BIN := $(BUILD_DIR)/bench_client

# make test가 생성하고 실행할 테스트 바이너리 목록이다.
TEST_BINS := \
	$(BUILD_DIR)/test_http_request \
	$(BUILD_DIR)/test_http_response \
	$(BUILD_DIR)/test_job_queue \
	$(BUILD_DIR)/test_server_app \
	$(BUILD_DIR)/test_engine_runtime

all: $(API_SERVER_BIN) $(BENCH_CLIENT_BIN)

# 빌드 산출물을 모아둘 디렉터리를 만든다.
$(BUILD_DIR):
	mkdir -p $@

$(API_SERVER_BIN): api_server/src/server/main.c $(API_SERVER_COMMON_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^

$(BENCH_CLIENT_BIN): bench/client/main.c bench/client/bench_client.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD_DIR)/test_http_request: api_server/tests/test_http_request.c api_server/src/http/http_request.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD_DIR)/test_http_response: api_server/tests/test_http_response.c api_server/src/http/http_response.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD_DIR)/test_job_queue: api_server/tests/test_job_queue.c api_server/src/concurrency/job_queue.c api_server/src/http/http_request.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD_DIR)/test_server_app: api_server/tests/test_server_app.c $(API_SERVER_COMMON_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD_DIR)/test_engine_runtime: db_engine/tests/test_engine_runtime.c $(DB_ENGINE_RUNTIME_SRCS) $(DB_ENGINE_CORE_SRCS) | $(BUILD_DIR)
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
