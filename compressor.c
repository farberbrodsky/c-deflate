#include "compressor.h"
#include "huffman.h"

struct state {
    FILE *dest;
    FILE *src;
    int bit_buf;                 // bit buffer to be written
    int bit_count;               // number of bits in bit buffer
    unsigned char in_buf[65536]; // used for repetitions
    int in_buf_index;            // index in in_buf
    jmp_buf except;
};

static void write_bits(int bits, int count, struct state *s) {
    unsigned long bit_buf = s->bit_buf;
    bit_buf = (bit_buf << count) | bits;
    s->bit_count += count;
    while (s->bit_count >= 8) {
        // write a byte
        unsigned char byte = bit_buf;
        fwrite(&byte, 1, 1, s->dest);
        bit_buf >>= 8;
        s->bit_count -= 8;
    }

    s->bit_buf = (int)(bit_buf);
}

static void flush_bits(struct state *s) {
    while (s->bit_count >= 8) {
        unsigned char byte = s->bit_buf;
        fwrite(&byte, 1, 1, s->dest);
        s->bit_buf >>= 8;
        s->bit_count -= 8;
    };
    // write remaining bits, which do not align to a byte, with 0s after them
    if (s->bit_count > 0) {
        unsigned char byte = s->bit_buf << (8 - s->bit_count);
        fwrite(&byte, 1, 1, s->dest);
    }
}

static unsigned char read_byte(struct state *s) {
    unsigned char byte;
    fread(&byte, 1, 1, s->src);
    if (s->in_buf_index == 65535) {
        // move everything 32768 bytes
        memmove(s->in_buf, s->in_buf + 32768, 32768);
        s->in_buf_index -= 32768;
    }
    s->in_buf[s->in_buf_index++] = byte;
    return byte;
}

int compressor(FILE *dest, FILE *src) {
    struct state s;
    s.src = src;
    s.dest = dest;
    s.bit_buf = 0;
    s.bit_count = 0;
    s.in_buf_index = 0;
    flush_bits(&s);
    return 0;
}
