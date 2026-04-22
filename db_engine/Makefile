CC     = gcc
CFLAGS = -std=c99 -Wall -Wextra -Iinclude

# db_engine 단독 CLI와 엔진 단위 테스트를 빌드하는 Makefile이다.
# 루트 Makefile과 별개로, db_engine 디렉터리 안에서 기존 엔진만 검증할 때 사용한다.

# 기본 실행 파일을 만드는 소스 목록
SRCS = src/main.c            \
       src/input/input.c     \
       src/input/lexer.c     \
       src/parser/parser.c   \
       src/schema/schema.c   \
       src/executor/executor.c \
       src/runtime/engine_runtime.c \
       src/bptree/bptree.c   \
       src/index/index_manager.c

TARGET = sqlp

# 기본 target은 SQL 파일을 실행하는 CLI 바이너리(sqlp)를 만든다.
all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

# 샘플 SQL 생성기
gen_data: tools/gen_data.c
	$(CC) $(CFLAGS) -o gen_data $^

# 단위 테스트 실행 파일
TEST_BINS = test_bptree test_index test_parser test_schema test_executor

test: $(TEST_BINS)
	@echo ""
	@echo "========== Running Tests =========="
	@./test_bptree   && echo "[PASS] bptree"   || echo "[FAIL] bptree"
	@./test_index    && echo "[PASS] index"    || echo "[FAIL] index"
	@./test_parser   && echo "[PASS] parser"   || echo "[FAIL] parser"
	@./test_schema   && echo "[PASS] schema"   || echo "[FAIL] schema"
	@./test_executor && echo "[PASS] executor" || echo "[FAIL] executor"
	@echo "==================================="

test_bptree: tests/test_bptree.c \
             src/bptree/bptree.c
	$(CC) $(CFLAGS) -o $@ $^

test_index: tests/test_index.c   \
            src/bptree/bptree.c  \
            src/index/index_manager.c \
            src/schema/schema.c \
            src/runtime/engine_runtime.c
	$(CC) $(CFLAGS) -o $@ $^

test_parser: tests/test_parser.c  \
             src/input/input.c    \
             src/input/lexer.c    \
             src/parser/parser.c
	$(CC) $(CFLAGS) -o $@ $^

test_schema: tests/test_schema.c \
             src/schema/schema.c \
             src/runtime/engine_runtime.c \
             src/bptree/bptree.c \
             src/index/index_manager.c
	$(CC) $(CFLAGS) -o $@ $^

test_executor: tests/test_executor.c  \
               src/schema/schema.c    \
               src/executor/executor.c \
               src/runtime/engine_runtime.c \
               src/bptree/bptree.c    \
               src/index/index_manager.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(TARGET) $(TEST_BINS) gen_data

.PHONY: all gen_data test clean
