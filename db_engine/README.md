# DB Engine

간단한 파일 기반 미니 DB 엔진입니다.
현재는 `SELECT`, `INSERT`, 스키마 검증, 그리고 `id`/`age`용 B+Tree 인덱스까지 포함한 C 프로젝트 상태입니다.

## 구조

- `include/`: 공용 헤더

- `src/`: 엔진 구현

- `tests/`: 단위 테스트

- `schema/`: 테이블 스키마 파일

- `samples/`: 실행용 SQL 예시

- `tools/`: 보조 도구

- `data/`: 실행 중 생성되는 데이터 파일

## 지원 범위

- `SELECT *` 또는 컬럼 목록 조회

- `WHERE col = value`

- `WHERE col BETWEEN a AND b`

- `INSERT INTO ... VALUES ...`

- `AUTO_INCREMENT` 컬럼 처리

- `id`, `age` 인덱스 검색

## 빌드

현재 `Makefile` 기준으로 Linux 계열 환경을 전제로 합니다.

```bash
make
make test
```

Windows 호스트에서는 `make`, `gcc`가 없으면 바로 실행되지 않으니,
devcontainer나 WSL 같은 Linux 환경에서 돌리는 편이 안전합니다.

## 실행 예시

```bash
./sqlp samples/select.sql
./sqlp samples/insert.sql
```

## 참고

- 스키마는 `schema/<table>.schema` 경로에서 읽습니다.

- 데이터 파일은 `data/<table>.dat` 경로에 생성됩니다.

- 현재 경로를 기준으로 파일을 읽기 때문에, 보통 `db_engine` 디렉터리 안에서 실행하는 것을 전제로 합니다.
