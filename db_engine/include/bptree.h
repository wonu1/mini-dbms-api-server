#ifndef BPTREE_H
#define BPTREE_H

/*
 * Public B+Tree API
 *
 * Tree #1: key=id,  value=file offset
 * Tree #2: key=age, value=file offset
 *
 * B+Tree는 정렬된 key를 빠르게 찾기 위한 자료구조다.
 * 여기서는 id나 age 값을 key로 쓰고, 실제 row가 data 파일 어디에 있는지
 * file offset을 value로 저장한다.
 */

typedef struct BPTree BPTree;

/* order 차수로 새 B+Tree를 만든다. */
BPTree *bptree_create(int order);

/* tree 전체 메모리를 해제한다. */
void    bptree_destroy(BPTree *tree);

/* key와 file offset value를 삽입한다. */
int  bptree_insert(BPTree *tree, int key, long value);

/* key 하나를 찾아 file offset을 반환한다. 없으면 -1을 반환한다. */
long bptree_search(BPTree *tree, int key);

/* from~to 범위에 속한 offset들을 out 배열에 채운다. */
int   bptree_range(BPTree *tree, int from, int to, long *out, int max_count);

/* from~to 범위 결과를 동적 배열로 만들어 반환한다. 호출자가 free한다. */
long *bptree_range_alloc(BPTree *tree, int from, int to, int *out_count);

#endif /* BPTREE_H */
