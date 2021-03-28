#ifndef GUARD_1e252457_729e_463f_bbcd_0d45520c797c
#define GUARD_1e252457_729e_463f_bbcd_0d45520c797c
#include "deflate.h"
#include <stdlib.h>
struct huffman {
    int value;             // isn't used if left or right aren't null
    struct huffman *left;  // represented by 0
    struct huffman *right; // represented by 1
};

// recursively frees a huffman tree
void huffman_free(struct huffman *huff);

// constructs a huffman tree from the lengths
struct huffman *huffman_construct(int *lengths, int count);

#ifdef DEFLATE_DEBUGGING
void huffman_print(struct huffman *huff);
#endif
#endif
