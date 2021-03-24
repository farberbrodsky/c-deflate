#include "decompressor2.h"

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
    long val = s->bit_buf;
    while (s->bit_count < count) {
        // read 8 more bits
        unsigned char eight_bits = 0;
        if (fread(&eight_bits, 1, 1, s->src) != 1) {
            // failed to read
            longjmp(s->except, ERR_INVALID_DEFLATE);
        }
        val |= (long)(eight_bits) << s->bit_count;
        s->bit_count += 8;
    }

    // remove count bits from buffer
    s->bit_buf = (int)(val >> count);
    s->bit_count -= count;

    // return the first count bits
    return (int)(val & ((1L << count) - 1));
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
