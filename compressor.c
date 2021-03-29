#include "compressor.h"
#include "huffman.h"

struct state {
    FILE *dest;
    FILE *src;
    bool eof;                    // whether file ended
    bool dry;                    // don't actually write this, only store how many bits it would be. not implemented yet.
    int bit_buf;                 // bit buffer to be written
    int bit_count;               // number of bits in bit buffer
    int in_buf_index;            // index in in_buf
    unsigned char in_buf[65536]; // used for repetitions
    int repetition_len[65536];   // length of best repetition, otherwise 0
    int repetition_dist[65536];  // distance of best repetition, otherwise 0
    int karp_rabin_3[65536];     // an identifying number for each 3-character repetition
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

// Reads a byte, saves it to in_buf and calculates karp_rabin_3
static unsigned char read_byte(struct state *s) {
    unsigned char byte;
    if (fread(&byte, 1, 1, s->src) != 1) {
        // file is done
        s->eof = true;
        return 0;
    }
    int i = s->in_buf_index++;
    s->in_buf[i] = byte;
    if (i >= 2) {
        // calculate karp-rabin for 3 characters
        s->karp_rabin_3[i - 2] =
            ((s->in_buf[i - 2])) |
            ((s->in_buf[i - 1]) << 8) |
            ((s->in_buf[i - 0]) << 16);
    }
    if (i == 65535) {
        // move everything 32768 bytes
        memmove(s->in_buf, s->in_buf + 32768, 32768);
        memmove(s->repetition_len, s->repetition_len + 32768, 32768);
        memmove(s->repetition_dist, s->repetition_dist + 32768, 32768);
        memmove(s->karp_rabin_3, s->karp_rabin_3 + 32768, 32768);
        s->in_buf_index = 32768;
    }
    return byte;
}

// iterates from s->in_buf_index - i to s->in_buf_index - j, and finds the best repetitions
static void find_repetitions(int i, int j, struct state *s) {
    i = s->in_buf_index - i;
    j = s->in_buf_index - j;
    for (; i < j; i++) {
        // go over the last 32768 bytes in the buffer, and look for a karp-rabin equality
        int r = i - 32768;
        if (r < 0) r = 0;
        int r_max = s->in_buf_index - 2; // there is no karp-rabin after r_max
        if (r_max >= i) r_max = i;       // don't repeat with yourself

        int best_repetition_r = 0;
        int best_repetition_length = 0;
        for (; r < r_max; r++) {
            // look for a repetition
            if (s->karp_rabin_3[r] == s->karp_rabin_3[i]) {
                // found a 3-character or more repetition! check for real length
                int repeat_len = 3;
                while (s->in_buf[r + repeat_len] == s->in_buf[i + repeat_len]) {
                    repeat_len++;
                }
                if (repeat_len >= best_repetition_length) { // we always prefer the later repetition because it's less bits
                    best_repetition_r = r;
                    best_repetition_length = repeat_len;
                }
            }
        }

        if (best_repetition_length != 0) {
            s->repetition_len[i] = best_repetition_length;
            s->repetition_dist[i] = i - best_repetition_r;
        }
    }
}

// TODO: iterates from s->in_buf_index - i to s->in_buf_index - j, writes it using dynamic huffman
// TODO: static int dynamic_huffman(int i, int j, char **str, struct state *s) {
// TODO: }

int compressor(FILE *dest, FILE *src) {
    struct state *s = malloc(sizeof(struct state));
    s->src = src;
    s->dest = dest;
    s->bit_buf = 0;
    s->bit_count = 0;
    s->in_buf_index = 0;
    // read 127 "chunks" of 258 per block, which is the maximum repeat length. only then can we find the best repetitions.
    for (int i = 0; i < 127; i++) {
        for (int j = 0; !s->eof && (j < 258); j++) {
            read_byte(s);
        }
        if (s->eof) {
            // file ended already :(
            find_repetitions(s->in_buf_index, 0, s);
        }
    }

    flush_bits(s);
    free(s);
    return 0;
}
