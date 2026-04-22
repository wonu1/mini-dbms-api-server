#ifndef BPTREE_H
#define BPTREE_H

/*
 * Public B+Tree API
 *
 * Tree #1: key=id,  value=file offset
 * Tree #2: key=age, value=file offset
 */

typedef struct BPTree BPTree;

BPTree *bptree_create(int order);
void    bptree_destroy(BPTree *tree);

int  bptree_insert(BPTree *tree, int key, long value);
long bptree_search(BPTree *tree, int key);

int   bptree_range(BPTree *tree, int from, int to, long *out, int max_count);
long *bptree_range_alloc(BPTree *tree, int from, int to, int *out_count);

#endif /* BPTREE_H */
