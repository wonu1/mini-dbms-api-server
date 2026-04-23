<div align="center">

# 🗄️ Mini DBMS API Server
**HTTP로 SQL을 보내면 JSON으로 결과를 돌려주는 미니 데이터베이스 서버**

<br><br>
<br><br>

## 개요

C언어로 만든 HTTP 기반 SQL 처리 서버다.  
클라이언트가 `curl`로 SQL을 보내면,   
서버가 파싱하고 실행해서 JSON으로 응답한다.

소켓, HTTP 파싱, JSON 생성, SQL 엔진,   
B+ Tree 인덱스, 스레드 풀, Atomic Lock까지   
외부 라이브러리 없이 전부 직접 구현했다.   
  
<br><br>
<br><br>


`C99` · `Socket` · `Thread Pool` · `B+ Tree` · `Atomic Lock` · **외부 라이브러리 0개**

---

</div>

<br><br>
<br><br>
<br><br>
<br><br>
## 아키텍처

<p align="center">
  <img src="./architecture.svg" alt="Architecture" width="100%"/>
</p>

<br><br>
<br><br>
<br><br>
<br><br>
<br><br>
<br><br>
## 쟁점
<!-- <img width="1280" height="900" alt="Pasted image 20260423010715" src="https://github.com/user-attachments/assets/e87de53f-2229-41b9-93f2-c5277513287e" /> -->
![이미지1](./image%20(2).png)
<br><br>
<!-- <img width="1280" height="900" alt="Pasted image 20260423011927" src="https://github.com/user-attachments/assets/23d7b36c-04c3-4dec-974e-fec3a0f24c3e" /> -->
![이미지1](./image%20(1).png)
<br><br>
<br><br>
<br><br>
<br><br>
<br><br>
<br><br>



## 엣지 케이스
 
### Content-Type 누락
 
`Content-Type: application/json` 헤더 없이 요청하면 거부한다.<br>
plain text로 SQL을 보내도 `invalid query payload`를 응답한다.
 
<br><br>
 
### 다중 SQL 문장 차단
 
`SELECT * FROM users; DROP TABLE users;` 같이<br>
세미콜론으로 여러 문장을 보내면 거부한다.<br>
`http_query_is_single_statement()`에서 SQL injection을 방지한다.
 
<br><br>
 
### 서버 재시작 시 Address already in use
 
이전 서버 프로세스가 남아있으면 같은 포트를 못 연다.<br>
`setsockopt(SO_REUSEADDR)`로 해결했고,<br>
그래도 안 되면 `ss -tlnp | grep 8080`으로 PID 찾아서 kill 해야 한다.
 
<br><br>
<br><br>
<br><br>
<br><br>
<br><br>
<br><br>
<br><br>


## 역할 분담
 
| 파트 | 담당 | 주요 파일 | 설명 |
|:----:|:----:|:---------|:-----|
| A. 서버 코어 | - | main.c, server_app.c, http_server.c | 서버 부팅, 소켓, 연결 수신 |
| B. HTTP 처리 | - | http_request.c, http_response.c | 요청 파싱, JSON 응답 생성 |
| C. 동시성 | - | thread_pool.c, job_queue.c | 스레드 풀, 작업 큐 |
| D. 엔진 연동 | - | engine_runtime.c, engine_api.c | Lock, DB 엔진 초기화 |
 


