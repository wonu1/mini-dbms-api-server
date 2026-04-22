#ifndef INDEX_MANAGER_H
#define INDEX_MANAGER_H

#define IDX_ORDER_DEFAULT  128
#define IDX_MAX_RANGE    65536
#define IDX_MAX_TABLES       8

/*
 * index_manager는 table별 B+Tree 인덱스를 관리한다.
 * 현재는 id 인덱스와 age 인덱스를 만들어 SELECT WHERE id/age 조건을 빠르게 처리한다.
 */

/* table의 data 파일을 읽어 id/age 인덱스를 초기화한다. */
int  index_init(const char *table, int order_id, int order_age);

/* 모든 table 인덱스를 정리한다. */
void index_cleanup(void);

/* id 인덱스에 key=id, value=file offset을 추가한다. */
int  index_insert_id(const char *table, int id, long offset);

/* AUTO_INCREMENT용 다음 id를 가져온다. */
int  index_next_auto_id(const char *table, int *out_id);

/* id 하나로 file offset을 찾는다. */
long index_search_id(const char *table, int id);

/* id 범위 검색 결과를 호출자가 제공한 배열에 채운다. */
int  index_range_id(const char *table, int from, int to,
                    long *offsets, int max);

/* id 범위 검색 결과를 동적 배열로 반환한다. */
long *index_range_id_alloc(const char *table, int from, int to,
                           int *out_count);

/* age 인덱스에 key=age, value=file offset을 추가한다. */
int  index_insert_age(const char *table, int age, long offset);

/* age 범위 검색 결과를 호출자가 제공한 배열에 채운다. */
int  index_range_age(const char *table, int from, int to,
                     long *offsets, int max);

/* age 범위 검색 결과를 동적 배열로 반환한다. */
long *index_range_age_alloc(const char *table, int from, int to,
                            int *out_count);

#endif /* INDEX_MANAGER_H */
