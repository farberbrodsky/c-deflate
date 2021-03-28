#ifndef GUARD_a29d25b7_0269_4ec8_8102_1c1271374a26
#define GUARD_a29d25b7_0269_4ec8_8102_1c1271374a26
#include "huffman.h"

void huffman_free(struct huffman *huff) {
    if (huff->left != NULL) {
        huffman_free(huff->left);
    }
    if (huff->right != NULL) {
        huffman_free(huff->right);
    }
    free(huff);
}

struct huffman *huffman_construct(int *lengths, int count) {
    // count the number of codes for each code length
    int bl_count[MAX_CODES] = {0};
    for (int i = 0; i < count; i++) {
        bl_count[lengths[i]] += 1;
    }
    bl_count[0] = 0;

    // generate the first code for each length
    int code = 0;
    int next_code[MAX_CODES + 1];
    for (int bits = 1; bits <= MAX_CODEBITS; ++bits) {
        code = (code + bl_count[bits - 1]) << 1;
        next_code[bits] = code;
    }

    // generate the huffman tree
    struct huffman *tree = (struct huffman *)calloc(1, sizeof(struct huffman));
    for (int i = 0; i < count; i++) {
        int code_len = lengths[i];
        if (code_len != 0) {
            uint16_t code = next_code[code_len]++;
            // we know the code, now we just need to put it on the tree
            struct huffman *node = tree;
            // read code bit by bit
            code <<= (16 - lengths[i]);
            for (int j = 0; j < lengths[i]; j++) {
                if (code & 0b1000000000000000) {
                    // go right
                    if (node->right == NULL) {
                        node->right = (struct huffman *)calloc(1, sizeof(struct huffman));
                    }
                    node = node->right;
                } else {
                    // go left
                    if (node->left == NULL) {
                        node->left = (struct huffman *)calloc(1, sizeof(struct huffman));
                    }
                    node = node->left;
                }
                code <<= 1;
            }
            node->value = i;
        }
    }
    return tree;
}

#ifdef DEFLATE_DEBUGGING
void huffman_print(struct huffman *huff) {
    if (huff == NULL) {
        printf("null");
    } else {
        printf("{\"val\": %d, \"L\": ", huff->value);
        huffman_print(huff->left);
        printf(", \"R\": ");
        huffman_print(huff->right);
        printf("}");
    }
}
#endif
#endif
