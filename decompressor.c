#include "decompressor.h"

#define MAX_CODEBITS 15   // a huffman code can't be more than 15 bits
#define MAX_CODES    286  // there are only 286 codes encoded

struct state {
    FILE *dest;
    FILE *src;
    int bit_buf;                  // bit buffer
    int bit_count;                // number of bits in bit buffer
    unsigned char out_buf[65536]; // used for repetitions
    int out_buf_index;            // index in out_buf
    jmp_buf except;
};

static int bits(struct state *s, int count) {
    long bit_buf = s->bit_buf;
    while (s->bit_count < count) {
        // read 8 more bits
        unsigned char eight_bits = 0;
        if (fread(&eight_bits, 1, 1, s->src) != 1) {
            // failed to read
            longjmp(s->except, ERR_INVALID_DEFLATE);
        }
        bit_buf |= (long)(eight_bits) << s->bit_count;
        s->bit_count += 8;
    }

    // remove count bits from buffer
    s->bit_buf = (int)(bit_buf >> count);
    s->bit_count -= count;

    // return the first count bits
    return (int)(bit_buf & ((1L << count) - 1));
}

static void write_byte(unsigned char byte, struct state *s) {
    fwrite(&byte, 1, 1, s->dest);
    if (s->out_buf_index == 65535) {
        // move everything 32768 bytes
        memmove(s->out_buf, s->out_buf + 32768, 32768);
        s->out_buf_index -= 32768;
    }
    s->out_buf[s->out_buf_index++] = byte;
}

static void write_repeat(int length, int distance, struct state *s) {
    if (s->out_buf_index < distance) {
        // this could have been a segfault! sheesh
        longjmp(s->except, ERR_INVALID_DEFLATE);
    }
    for (int i = 0; i < length; i++) {
        write_byte(s->out_buf[s->out_buf_index++ - distance], s);
    }
}

static void non_compressed_block(struct state *s) {
    // read big-endian 16 bit number for length, and then nlen (one's complement of len)
    unsigned int block_length = be16toh(bits(s, 16));
    unsigned int block_nlen   = be16toh(bits(s, 16));
#ifdef DEFLATE_DEBUGGING
    printf("non-compressed block: %u bytes\n", block_length);
#endif
    // TODO: check that nlen is one's complement of len
    // read block_length bytes
    for (unsigned int i = 0; i < block_length; i++) {
        int byte = bits(s, 8);
        write_byte(byte, s);
    }
}

// constructs a huffman tree from the lengths
struct huffman {
    int value;             // isn't used if left or right aren't null
    struct huffman *left;  // represented by 0
    struct huffman *right; // represented by 1
};

static void huffman_free(struct huffman *huff) {
    if (huff->left != NULL) {
        huffman_free(huff->left);
    }
    if (huff->right != NULL) {
        huffman_free(huff->right);
    }
    free(huff);
}

static struct huffman *huffman_construct(int *lengths, int count) {
    // count the number of codes for each code length
    int bl_count[MAX_CODES];
    memset(bl_count, 0, MAX_CODES);
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
    struct huffman *tree = calloc(1, sizeof(struct huffman));
    for (int i = 0; i < count; i++) {
        int code_len = lengths[i];
        if (code_len != 0) {
            uint16_t code = next_code[code_len]++;
            // we know the code, now we just need to put it on the tree
            struct huffman *node = tree;
            // read code bit by bit
            bool found_first_bit = false;
            code <<= (16 - lengths[i]);
            for (int j = 0; j < lengths[i]; j++) {
                if (code & 0b1000000000000000) {
                    found_first_bit = true;
                    // go right
                    if (node->right == NULL) {
                        node->right = calloc(1, sizeof(struct huffman));
                    }
                    node = node->right;
                } else {
                    // go left
                    if (node->left == NULL) {
                        node->left = calloc(1, sizeof(struct huffman));
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

// Reads the next character according to a huffman code
static int huffman_read_next(struct huffman *huff, struct state *s) {
    while (huff->left || huff->right) {
        int bit = bits(s, 1);
        if (bit) {
            huff = huff->right;
        } else {
            huff = huff->left;
        }
    }
    return huff->value;
}

#ifdef DEFLATE_DEBUGGING
static void huffman_print(struct huffman *huff) {
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

static void read_with_huffman(struct huffman *literal_huff, struct huffman *distnce_huff, struct state *s) {
    int literal = huffman_read_next(literal_huff, s);
    while (literal != 256) { // 256 is end of block
        if (literal < 256) {
            write_byte(literal, s);
        } else {
            int repeat_len;
            if (literal <= 268) {
                // 1 extra bit
                repeat_len = ((literal - 265) * 2 + 11) + bits(s, 1);
            } else if (literal <= 272) {
                // 2 extra bits
                repeat_len = ((literal - 269) * 4 + 19) + bits(s, 2);
            } else if (literal <= 276) {
                // 3 extra bits
                repeat_len = ((literal - 273) * 8 + 35) + bits(s, 3);
            } else if (literal <= 280) {
                // 4 extra bits
                repeat_len = ((literal - 277) * 16 + 67) + bits(s, 4);
            } else if (literal <= 284) {
                // 5 extra bits
                repeat_len = ((literal - 281) * 32 + 131) + bits(s, 5);
            } else {
                // 285 is 258
                repeat_len = 258;
            }
            // read distance code
            int dist = huffman_read_next(distnce_huff, s);
            write_repeat(repeat_len, dist, s);
        }
        literal = huffman_read_next(literal_huff, s);
    }
}

static void dynamic_huffman_block(struct state *s) {
    int hlit = 257 + bits(s, 5); // number of Literal/Length codes - 257-286
    int hdist =  1 + bits(s, 5); // number of Distance codes - 1-32
    int hclen =  4 + bits(s, 4); // number of Code Length codes - 4-19
    int code_lengths[19];
    for (int i = 0; i < hclen; i++) {
        // read the 3-bit code length
        code_lengths[i] = bits(s, 3);
    }
    // for some reason, the order of the code lengths is not sorted
    // so we need to reorder it before constructing our huffman tree
    int code_lengths_ordered[] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    for (int i = 0; i < hclen; i++) {
        code_lengths_ordered[i] = code_lengths[code_lengths_ordered[i]];
    }
    struct huffman *code_lengths_huff = huffman_construct(code_lengths_ordered, hclen);

    // read huffman for literal/length alphabet
    // code length repeat codes can cross from hlit to hdist
    int *dist_and_lit_huffman_lengths = malloc((hlit + hdist) * sizeof(int));
    for (int i = 0; i < hlit + hdist; i++) {
        int code_length = huffman_read_next(code_lengths_huff, s);
        if (code_length <= 15) {
            // length code of 0-15
            dist_and_lit_huffman_lengths[i] = code_length;
        } else if (code_length == 16) {
            // copy previous 3-6 times, 2 extra bits
            int repeat_length = 3 + bits(s, 2);

            if (i == 0) {
                // this could have been a segfault! sheesh
                free(dist_and_lit_huffman_lengths);
                huffman_free(code_lengths_huff);
                longjmp(s->except, ERR_INVALID_DEFLATE);
            }

            int repeat_this = dist_and_lit_huffman_lengths[i - 1];
            for (int j = 0; j < repeat_length; ++j) {
                dist_and_lit_huffman_lengths[i++] = repeat_this;
            }
            --i; // that was 1 too many
        } else if (code_length == 17) {
            // repeat 0 for 3-10 times
            int repeat_length = 3 + bits(s, 3);
            for (int j = 0; j < repeat_length; ++j) {
                dist_and_lit_huffman_lengths[i++] = 0;
            }
            --i; // that was 1 too many
        } else {
            // repeat 0 for 11-138 times
            int repeat_length = 11 + bits(s, 7);
            for (int j = 0; j < repeat_length; ++j) {
                dist_and_lit_huffman_lengths[i++] = 0;
            }
            --i; // that was 1 too many
        }
    }
    huffman_free(code_lengths_huff);
    // construct literal and distance huffman codes
    struct huffman *literal_huff = huffman_construct(dist_and_lit_huffman_lengths, hlit);
    struct huffman *distnce_huff = huffman_construct(dist_and_lit_huffman_lengths + hlit, hdist);
    free(dist_and_lit_huffman_lengths);
    read_with_huffman(literal_huff, distnce_huff, s);
    // free unused stuff
    huffman_free(literal_huff);
    huffman_free(distnce_huff);
}

static void static_huffman_block(struct state *s) {
    // construct huffman
    int lengths[288];
    int i;
    for (i = 0; i < 144; ++i) {
        lengths[i] = 8;
    }
    for (; i < 256; ++i) {
        lengths[i] = 9;
    }
    for (; i < 280; ++i) {
        lengths[i] = 7;
    }
    for (; i < 288; ++i) {
        lengths[i] = 8;
    }
    struct huffman *literal_huff = huffman_construct(lengths, 288);
    for (i = 0; i < 30; ++i) {
        lengths[i] = 5;
    }
    struct huffman *distnce_huff = huffman_construct(lengths, 30);
    // read as usual
    read_with_huffman(literal_huff, distnce_huff, s);
    huffman_free(literal_huff);
    huffman_free(distnce_huff);
}

int decompressor(FILE *dest, FILE *src) {
    struct state s;
    s.src = src;
    s.dest = dest;
    s.bit_buf = 0;
    s.bit_count = 0;
    s.out_buf_index = 0;
    
    int exception = setjmp(s.except);
    if (exception != 0) {
        return exception;
    } else {
        // decompress block by block
        int is_last = 0;
        // first bit - whether this is the final block
        while (is_last == 0) {
            is_last = bits(&s, 1);
            // next 2 bits - whether this block is non-compressed, static huffman or dynamic huffman
            int block_type = bits(&s, 2);
            switch (block_type) {
                case 0b00:
                    // non-compressed
                    non_compressed_block(&s);
                    break;
                case 0b01:
                    // static huffman
#ifdef DEFLATE_DEBUGGING
                    printf("static huffman block\n");
#endif
                    static_huffman_block(&s);
                    break;
                case 0b10:
                    // dynamic huffman
#ifdef DEFLATE_DEBUGGING
                    printf("dynamic huffman block\n");
#endif
                    dynamic_huffman_block(&s);
                    break;
                default:
                    // invalid!
                    return 1;
            }
        }
    }
    return 0;
}
