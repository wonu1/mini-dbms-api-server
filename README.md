# Mini DBMS API Server

> HTTP로 SQL을 보내면 JSON으로 결과를 돌려주는 미니 데이터베이스 서버

크래프톤 정글 SW-AI 12기 | CSAPP Chapter 11 네트워크 프로그래밍

---

## 프로젝트 소개

C언어로 만든 HTTP 기반 SQL 처리 서버다. 클라이언트가 `curl`로 SQL을 보내면, 서버가 파싱하고 실행해서 JSON으로 응답한다. 외부 라이브러리 없이 소켓, HTTP 파싱, JSON 생성, SQL 엔진, B+ Tree 인덱스, 스레드 풀까지 전부 직접 구현했다.

---

## 프로그램 흐름

```
0. 서버 부팅 (소켓 생성 → bind → listen)
1. 클라이언트 연결 (accept)
2. HTTP POST 요청 수신
3. 요청 파싱 → QueryJob 생성
4. Job Queue에 삽입
5. 워커 스레드가 Job을 꺼내서 처리
6. SQL 파싱 (Lexer → Parser → Schema 검증)
7. DB 엔진 실행 (Lock 획득 → SELECT/INSERT → Lock 해제)
8. 결과를 JSON으로 변환
9. HTTP 응답 반환 → 연결 종료
```

---

## 아키텍처

```
┌─────────────────────────────────────────────────────┐
│                    API Server                        │
│                                                     │
│  ┌──────────┐    ┌───────────┐    ┌──────────────┐  │
│  │  소켓     │    │ Job Queue │    │ Thread Pool  │  │
│  │ (accept) │───▶│ (원형버퍼) │───▶│ (워커 N개)   │  │
│  └──────────┘    └───────────┘    └──────┬───────┘  │
│                                          │          │
│  ┌──────────────┐    ┌───────────────────▼────────┐ │
│  │ HTTP 파서    │    │ engine_execute_sql()        │ │
│  │ (JSON 추출)  │    │ ┌─ Read/Write Lock ───────┐ │ │
│  └──────────────┘    │ │                         │ │ │
│                      │ │  Lexer → Parser →       │ │ │
│  ┌──────────────┐    │ │  Schema → Executor      │ │ │
│  │ HTTP 응답    │    │ │                         │ │ │
│  │ (JSON 생성)  │    │ └─────────────────────────┘ │ │
│  └──────────────┘    └────────────────────────────┘ │
│                                                     │
└─────────────────────────────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────┐
│                    DB Engine                         │
│                                                     │
│  ┌────────┐  ┌────────┐  ┌──────────┐  ┌────────┐  │
│  │ Lexer  │─▶│ Parser │─▶│ Schema   │─▶│Executor│  │
│  │토큰 분리│  │AST 생성│  │스키마 검증│  │실행/저장│  │
│  └────────┘  └────────┘  └──────────┘  └────┬───┘  │
│                                              │      │
│  ┌──────────────┐    ┌───────────────────────┘      │
│  │ Index Manager│    │                              │
│  │ (B+ Tree x2) │◀───┘                              │
│  │  id / age    │    ┌──────────────┐               │
│  └──────────────┘    │ .dat 파일    │               │
│                      │ (파이프 구분) │               │
│                      └──────────────┘               │
└─────────────────────────────────────────────────────┘
```

---

## 폴더 구조

```
mini-dbms-api-server/
├── api_server/
│   ├── include/                 # 서버 헤더 파일
│   ├── src/
│   │   ├── server/              # main.c, server_app.c, http_server.c
│   │   ├── http/                # http_request.c, http_response.c
│   │   └── concurrency/         # thread_pool.c, job_queue.c
│   └── tests/
├── db_engine/
│   ├── include/                 # interface.h, bptree.h, index_manager.h
│   ├── src/
│   │   ├── input/               # lexer.c
│   │   ├── parser/              # parser.c
│   │   ├── schema/              # schema.c
│   │   ├── executor/            # executor.c
│   │   ├── bptree/              # bptree.c
│   │   ├── index/               # index_manager.c
│   │   └── runtime/             # engine_runtime.c, engine_api.c
│   ├── schema/                  # .schema 파일 (테이블 정의)
│   ├── data/                    # .dat 파일 (실제 데이터)
│   └── tests/
├── Makefile
└── README.md
```

---

## 빌드 및 실행

### 빌드

```bash
make
```

### 서버 실행

```bash
./build/api_server_stub --port 8080 --workers 4
```

> `db_engine/data/` 디렉토리가 없으면 미리 생성: `mkdir -p db_engine/data`
> 반드시 **프로젝트 루트**에서 실행해야 한다 (스키마 경로가 상대경로)

### INSERT

```bash
curl -X POST http://localhost:8080/query \
  -H "Content-Type: application/json" \
  -d '{"sql": "INSERT INTO users (name, age, email) VALUES ('\''alice'\'', '\''25'\'', '\''alice@test.com'\'')"}'
```

### SELECT

```bash
# 전체 조회
curl -s -X POST http://localhost:8080/query \
  -H "Content-Type: application/json" \
  -d '{"sql": "SELECT * FROM users"}' | python3 -m json.tool

# WHERE 조건
curl -s -X POST http://localhost:8080/query \
  -H "Content-Type: application/json" \
  -d '{"sql": "SELECT * FROM users WHERE id = 1"}' | python3 -m json.tool

# BETWEEN 범위 검색
curl -s -X POST http://localhost:8080/query \
  -H "Content-Type: application/json" \
  -d '{"sql": "SELECT * FROM users WHERE age BETWEEN 20 AND 30"}' | python3 -m json.tool
```

> `python3 -m json.tool`을 파이프하면 JSON이 보기 좋게 출력된다

---

## 지원하는 SQL

| SQL | 예시 |
|-----|------|
| INSERT (컬럼 지정) | `INSERT INTO users (name, age, email) VALUES ('bob', '30', 'bob@test.com')` |
| SELECT 전체 | `SELECT * FROM users` |
| SELECT 컬럼 지정 | `SELECT name, age FROM users` |
| WHERE 동등 | `SELECT * FROM users WHERE id = 1` |
| WHERE 범위 | `SELECT * FROM users WHERE age BETWEEN 20 AND 30` |

- id 컬럼은 AUTO_INCREMENT (INSERT 시 자동 채번)
- 한 번에 SQL 1개만 실행 가능 (보안 목적)

---

## 스키마 정의

`db_engine/schema/users.schema`:

```
table=users
columns=4
col0=id,INT,0,PK,AUTO_INCREMENT
col1=name,VARCHAR,64
col2=age,INT,0
col3=email,VARCHAR,128
```

---

## 벤치마크

`bench_client`로 INSERT 100건 동시 요청 시:

| Workers | 소요 시간 | 비고 |
|---------|----------|------|
| 1 | 0.132s | 단일 스레드 |
| 4 | 0.092s | CPU 코어 수와 일치, **최적** |
| 8 | 0.117s | 컨텍스트 스위칭 오버헤드 발생 |

```bash
# 벤치마크 실행
make bench
./build/bench_client --host 127.0.0.1 --port 8080 --requests 100 --concurrency 10
```

---

## 쟁점

### 스레드 풀의 스레드 수는 몇 개가 최적인가?

CPU 코어 수와 동일할 때 가장 빨랐다 (4코어 → workers 4). 스레드가 코어보다 많으면 OS가 스레드를 번갈아 실행하면서 컨텍스트 스위칭 비용이 발생한다. 벤치마크에서 workers 8이 workers 4보다 느린 이유가 이것이다.

### INSERT에 왜 Lock을 거는가?

두 스레드가 동시에 같은 `.dat` 파일에 `fwrite`하면 데이터가 섞인다. 예를 들어 스레드 A가 `"1 | alice | 25"` 쓰는 중간에 스레드 B가 `"2 | bob | 30"`을 끼워 넣으면 `"1 | ali2 | bob | 30ce | 25"` 같은 깨진 데이터가 저장된다. exclusive lock으로 INSERT는 한 번에 하나만 실행되게 보장한다.

### Shared Lock은 왜 필요한가? 안 걸면 안 되나?

SELECT에 lock을 안 걸면 INSERT가 파일을 쓰는 도중에 SELECT가 반쯤 쓰인 줄을 읽을 수 있다. shared lock이 있으면 INSERT(exclusive lock)가 대기한다. 대신 SELECT끼리는 동시에 읽을 수 있어서 읽기 성능은 유지된다.

### Job Queue 크기는 얼마가 적절한가?

큐가 너무 작으면 accept한 연결이 큐에 못 들어가서 대기하고, 너무 크면 메모리를 낭비한다. 일반적으로 workers 수의 2~4배 정도가 적당하다.

---

## 기술 스택

- **언어**: C (C99)
- **외부 라이브러리**: 없음 (전부 직접 구현)
- **빌드**: GNU Make + gcc
- **동시성**: POSIX Threads (pthread)
- **Lock**: Atomic operations (`__atomic_compare_exchange_n`)
- **인덱스**: B+ Tree (차수 128, id/age 컬럼)
- **데이터 저장**: 파이프 구분 텍스트 파일 (.dat)

---

## 팀 역할

| 파트 | 담당 | 주요 파일 |
|------|------|----------|
| A. 서버 코어 | - | main.c, server_app.c, http_server.c |
| B. HTTP 처리 | - | http_request.c, http_response.c |
| C. 동시성 | - | thread_pool.c, job_queue.c |
| D. 엔진 연동 | - | engine_runtime.c, engine_api.c |

---

## Git 브랜치 전략

```
main ← dev ← feature/A-server-core
              feature/B-http-handler
              feature/C-concurrency
              feature/D-engine-bridge
```

각 팀원이 feature 브랜치에서 개발 → dev에 머지 → 최종 main 머지
