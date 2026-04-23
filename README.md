<div align="center">

# 🗄️ Mini DBMS API Server
**HTTP로 SQL을 보내면 JSON으로 결과를 돌려주는 미니 데이터베이스 서버**

`C99` · `Socket` · `Thread Pool` · `B+ Tree` · `Atomic Lock` · **외부 라이브러리 0개**

---

</div>

## 개요

C언어로 만든 HTTP 기반 SQL 처리 서버다.  
클라이언트가 `curl`로 SQL을 보내면 서버가 파싱하고 실행해서 JSON으로 응답한다.

소켓, HTTP 파싱, JSON 생성, SQL 엔진,  
B+ Tree 인덱스, 스레드 풀, Atomic Lock까지  
외부 라이브러리 없이 전부 직접 구현했다.

이 저장소는 크게 두 층으로 나뉜다.

- `db_engine/`: 파일 기반 mini DBMS 코어
- `api_server/`: HTTP 요청을 받아 SQL 엔진을 호출하는 서버 레이어

## 아키텍처

<p align="center">
  <img src="./architecture.svg" alt="Architecture" width="100%"/>
</p>

## 쟁점

핵심 구현 포인트는 아래와 같다.

- **엔진/서버 경계 분리**
  - 서버는 SQL 문자열을 받고, 엔진 런타임이 파싱·검증·실행·결과 변환을 맡는다.
- **파일 기반 저장 구조**
  - 스키마는 `db_engine/schema/*.schema`, 데이터는 `db_engine/data/*.dat`에 저장된다.
- **인덱스 최적화**
  - `id`, `age` 컬럼에 대해 B+ Tree 인덱스를 사용한다.
- **동시성 처리**
  - HTTP 서버는 thread pool + job queue로 요청을 병렬 처리한다.

구현/실험 관점에서 참고할 그림:

![이미지1](./image%20(2).png)

![이미지2](./image%20(1).png)

## 핵심 기능

- `GET /health` 헬스 체크
- `POST /query` SQL 실행 API
- `SELECT` 결과를 JSON `columns + rows + row_count`로 반환
- `INSERT` 결과를 JSON `affected_rows + generated_id`로 반환
- `WHERE col = value`
- `WHERE col BETWEEN a AND b`
- `AUTO_INCREMENT` 처리
- `id`, `age` B+ Tree 인덱스
- `bench_client` 및 worker sweep 스크립트 제공

## 프로젝트 구조

```text
mini-dbms-api-server/
├─ api_server/        # HTTP 서버, 요청/응답 파싱, 큐, 스레드풀
├─ db_engine/         # 파일 기반 mini DBMS 코어
├─ bench/             # 벤치 클라이언트, smoke/sweep 스크립트
├─ build/             # 루트 빌드 산출물
├─ architecture.svg   # 구조도
├─ Makefile           # 루트 빌드/테스트
└─ README.md
```

## 요구 환경

이 저장소는 Linux 계열 환경을 기준으로 작성되어 있다.

- 권장 환경
  - devcontainer
  - WSL
  - 일반 Linux
- 필요 도구
  - `make`
  - `gcc` 또는 C99 컴파일러
  - `bash`
  - `curl`

주의:

- Windows PowerShell 호스트에서는 `make`가 없어서 바로 빌드가 안 될 수 있다.
- 실제 실행은 devcontainer/WSL/Linux 터미널을 권장한다.

## 빠른 시작

### 빌드

```bash
cd /workspaces/mini-dbms-api-server
make all
```

정리 후 다시 빌드:

```bash
make clean
make all
```

생성되는 주요 바이너리:

- `build/api_server_stub`
- `build/bench_client`

### 서버 실행

기본 실행:

```bash
./build/api_server_stub
```

권장 예시:

```bash
./build/api_server_stub --port 8080 --workers 6 --queue-capacity 256
```

옵션:

```text
./build/api_server_stub [--port PORT] [--workers N] [--queue-capacity N]
  --port PORT            listening TCP port (default 8080, range 1..65535)
  --workers N            worker thread count (default 4, max 1024)
  --queue-capacity N     job queue capacity (default 64, max 65536)
```

### 실행 예시

헬스 체크:

```bash
curl -sS http://127.0.0.1:8080/health | python3 -m json.tool
```

SELECT:

```bash
curl -sS -X POST http://127.0.0.1:8080/query \
  -H "Content-Type: application/json" \
  --data '{"sql":"SELECT * FROM users WHERE id = 1;"}' | python3 -m json.tool
```

INSERT:

```bash
curl -sS -X POST http://127.0.0.1:8080/query \
  -H "Content-Type: application/json" \
  --data '{"sql":"INSERT INTO users (name, age, email) VALUES ('\''demo_user'\'', 30, '\''demo_user@example.com'\'');"}' | python3 -m json.tool
```

## API 요약

### `GET /health`

- 응답 코드: `200 OK`
- 응답 본문:

```json
{
  "status": "ok"
}
```

### `POST /query`

요청 헤더:

```http
Content-Type: application/json
```

요청 바디:

```json
{
  "sql": "SELECT * FROM users WHERE id = 1;"
}
```

성공 응답 형태:

```json
{
  "status": "ok",
  "data": {
    "columns": ["id", "name", "age", "email"],
    "rows": [["1", "kim", "30", "kim@example.com"]],
    "row_count": 1
  }
}
```

또는:

```json
{
  "status": "ok",
  "data": {
    "affected_rows": 1,
    "generated_id": 123
  }
}
```

에러 응답 형태:

```json
{
  "status": "error",
  "error": {
    "code": "PARSE_ERROR",
    "message": "..."
  }
}
```

대표 에러 코드:

- `PARSE_ERROR`
- `VALIDATION_ERROR`
- `UNSUPPORTED_SQL`
- `ENGINE_RUNTIME_ERROR`
- `NOT_IMPLEMENTED`

## 지원 SQL 범위

- `SELECT * FROM ...`
- `SELECT col1, col2 FROM ...`
- `WHERE col = value`
- `WHERE col BETWEEN a AND b`
- `INSERT INTO ... (col1, col2, ...) VALUES (...)`
- `AUTO_INCREMENT` 컬럼 처리
- `id`, `age` 인덱스 검색

현재 범위 밖으로 보는 것이 안전한 기능:

- `JOIN`
- `ORDER BY`
- `GROUP BY`
- `UPDATE`
- `DELETE`
- 복수 statement 실행

## 제한 사항

- `Content-Type`은 정확히 `application/json`이어야 한다.
- 바디는 `{"sql":"..."}` 형태여야 한다.
- SQL은 **단일 statement만 허용**된다.
- `AUTO_INCREMENT` 컬럼은 직접 넣지 않는다.

예:

```sql
INSERT INTO users (name, age, email) VALUES (...)
```

## 엣지 케이스

### Content-Type 누락

`Content-Type: application/json` 헤더 없이 요청하면 거부한다.  
plain text로 SQL을 보내도 `invalid query payload`를 응답한다.

### 다중 SQL 문장 차단

`SELECT * FROM users; DROP TABLE users;` 같이  
세미콜론으로 여러 문장을 보내면 거부한다.  
`http_query_is_single_statement()`에서 이를 차단한다.

### 서버 재시작 시 Address already in use

이전 서버 프로세스가 남아있으면 같은 포트를 못 연다.  
`setsockopt(SO_REUSEADDR)`로 해결했고,  
그래도 안 되면 `ss -tlnp | grep 8080`으로 PID를 찾아 정리해야 한다.

## 역할 분담

| 파트 | 담당 | 주요 파일 | 설명 |
|:----:|:----:|:---------|:-----|
| A. 서버 코어 | - | `main.c`, `server_app.c`, `http_server.c` | 서버 부팅, 소켓, 연결 수신 |
| B. HTTP 처리 | - | `http_request.c`, `http_response.c` | 요청 파싱, JSON 응답 생성 |
| C. 동시성 | - | `thread_pool.c`, `job_queue.c` | 스레드 풀, 작업 큐 |
| D. 엔진 연동 | - | `engine_runtime.c`, `engine_api.c` | Lock, DB 엔진 초기화 |

## 테스트

루트 테스트:

```bash
cd /workspaces/mini-dbms-api-server
make test
```

주요 대상:

- `test_http_request`
- `test_http_response`
- `test_job_queue`
- `test_server_app`
- `test_engine_runtime`

엔진 테스트:

```bash
cd /workspaces/mini-dbms-api-server/db_engine
make test
```

주요 대상:

- `bptree`
- `index`
- `parser`
- `schema`
- `executor`

현재 README 정리 시점 기준으로 devcontainer 안에서 아래를 실제로 확인했다.

- `make all` 통과
- 루트 `make test` 통과
- `db_engine/make test` 통과

## 벤치마크

벤치 클라이언트:

```text
./build/bench_client [--host HOST] [--port PORT] [--workers N] [--requests N] [--scenario select|select_range_1000|insert]
```

기본 시나리오:

- `select`
- `select_range_1000`
- `insert`

예시:

```bash
./build/bench_client --host 127.0.0.1 --port 8080 --workers 8 --requests 20000 --scenario select
```

worker sweep:

```bash
SWEEP_WORKERS_START=1 \
SWEEP_WORKERS_END=12 \
SWEEP_CLIENT_WORKERS=32 \
SWEEP_REQUESTS=10000 \
SWEEP_REPEAT=1 \
bash bench/scripts/run_worker_sweep.sh
```

### 벤치 그래프 예시

단일 `SELECT` 처리량:

<p align="center">
  <img src="./bench/results/image_digitized_user_worker_sweep_20260423/select_stabilized_only_title_clean.png" alt="Select Throughput" width="100%"/>
</p>

**짧은 `SELECT` 해석**

- 이 그래프는 상대적으로 짧은 단건 `SELECT`를 반복한 경우라서, 요청 하나를 처리하는 데 필요한 일이 크지 않다.
- 그래서 초반에는 worker를 늘릴수록 빨라지지만, `worker=2` 정도만 되어도 이미 병렬 처리 이득의 대부분을 얻는다.
- 그 이후에는 worker를 더 늘려도 CPU가 할 일이 크게 늘지 않기 때문에, 오히려 스케줄링 오버헤드나 context switch 비용이 상대적으로 더 두드러질 수 있다.
- 즉 이 경우에는 "worker를 많이 두는 것"보다 "적은 worker로 빠르게 처리하는 것"이 더 잘 맞는다.

`INSERT`와 `SELECT BETWEEN` 비교:

<p align="center">
  <img src="./bench/results/image_digitized_user_worker_sweep_20260423/insert_and_select_between.png" alt="Insert and Select Between Throughput" width="100%"/>
</p>

**`SELECT BETWEEN 1 AND 100` 해석**

- `SELECT BETWEEN`은 짧은 단건 조회보다 한 번에 더 많은 범위를 읽기 때문에 CPU와 엔진 처리 비용이 더 크게 걸린다.
- 그래서 worker를 늘렸을 때 짧은 `SELECT`보다 더 긴 구간에서 성능 이득이 유지되고, 이 그래프에서는 `worker=7` 근처에서 가장 높은 처리량 `18,966 req/s`를 보인다.
- 다만 이런 피크 위치는 실험 환경의 코어 수나 스케줄링 조건에 따라 달라질 수 있다.
- 또한 worker를 계속 늘린다고 무조건 좋아지는 것도 아니다. 어느 지점을 지나면 context switch, 큐 경쟁, 런타임 동기화 비용이 더 커져서 처리량이 다시 떨어지거나 비슷한 수준에서 정체될 수 있다.

**`INSERT` 해석**

- `INSERT` 경로는 동시성 보호를 위해 쓰기 구간이 잠금으로 묶여 있어서, 읽기처럼 자유롭게 병렬 처리되지 않는다.
- 그래서 `worker=2`에서 `7,568 req/s`로 최고점을 보인 뒤에는 worker를 더 늘려도 뚜렷한 개선이 나타나지 않는다.
- 결국 이 경우에는 요청을 여러 스레드에 나눠도 실제 핵심 쓰기 작업은 순차적으로 처리되기 때문에, worker를 많이 늘리는 것이 큰 의미를 갖지 않는다.

## DB 엔진 단독 사용

```bash
cd /workspaces/mini-dbms-api-server/db_engine
make
./sqlp samples/select.sql
./sqlp samples/insert.sql
```

더 자세한 엔진 설명은 [db_engine/README.md](./db_engine/README.md)를 보면 된다.
