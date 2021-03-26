#include "decompressor.h"

#define MAX_CODEBITS 15   // a huffman code can't be more than 15 bits
#define MAX_CODES    286  // there are only 286 codes encoded

struct state {
    FILE *dest;
    FILE *src;
    int bit_buf;   // bit buffer
    int bit_count; // number of bits in bit buffer
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
        fwrite(&byte, 1, 1, s->dest);
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

#ifdef DEFLATE_DEBUGGING
static void huffman_print(struct huffman *huff) {
    if (huff == NULL) {
        printf("NULL");
    } else {
        printf("Huff%d(L: ", huff->value);
        huffman_print(huff->left);
        printf(", R: ");
        huffman_print(huff->right);
        printf(")");
    }
}
#endif

static void dynamic_huffman_block(struct state *s) {
    int hlit = 257 + bits(s, 5); // number of Literal/Length codes - 257-286
    int hdist =  1 + bits(s, 5); // number of Distance codes - 1-32
    int hclen =  4 + bits(s, 4); // number of Code Length codes - 4-19
    // code length repeat codes can cross from HLIT+257 to HDIST+1, so we have a sequence of HLIT+257+HDIST+1 code lengths.
    int *code_lengths = malloc(sizeof(int) * (hlit + hdist + 258));
    for (int i = 0; i < (hclen + 4); i++) {
        // read the 3-bit code length
        code_lengths[i] = bits(s, 3);
    }
    // distance codes' huffman is encoded with the code length Huffman code
}

int decompressor(FILE *dest, FILE *src) {
    struct state s;
    s.src = src;
    s.dest = dest;
    s.bit_buf = 0;
    s.bit_count = 0;
    
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
                    break;
                case 0b10:
                    // dynamic huffman
                    break;
                default:
                    // invalid!
                    return 1;
            }
        }
    }
    return 0;
}
