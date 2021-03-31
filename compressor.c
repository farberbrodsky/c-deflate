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
    bit_buf = bit_buf | (bits << s->bit_count);
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

static int binary_search(const int *arr, int R, int x) {
    int L = 0;
    while (L != R) {
        int mid = L + (R - L) / 2;
        if (x < arr[mid]) {
            R = mid;
        } else {
            L = mid + 1;
        }
    }
    return L - 1;
}

// iterates from s->in_buf_index - i to s->in_buf_index - j, writes it using dynamic huffman
static const int lengths_for_codes[29]   = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                             35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const int lengths_for_repeats[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97,
                                             129, 193, 257, 385, 513, 769, 1025, 1537, 2049,
                                             3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const short lengths_extra_bits[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const short dist_extra_bits[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11,
    12, 12, 13, 13};
static void dynamic_huffman(int i, int j, struct state *s) {
    i = s->in_buf_index - i;
    j = s->in_buf_index - j;
    // step 1: choose the best repetitions...
    // step 2: find how many times each character in 0-285 appears, and every distance code in 1-32,
    // and calculate hlit (number of literal/length codes) and hdist (number of distance codes)
    int hlit = 0;
    int hdist = 0;
    int dist_and_lit_huffman_appearances[286 + 32] = {0};
    for (; i < j; i++) {
        int replen = s->repetition_len[i];
        if (replen != 0) {
            // find correct length code with binary search
            int lit = binary_search(lengths_for_codes, 29, replen);
            dist_and_lit_huffman_appearances[257 + lit] += 1;
            if ((lit + 1) > hlit) {
                hlit = lit + 1;
            }
            // find correct repeat code with binary search
            int dist = binary_search(lengths_for_repeats, 30, s->repetition_dist[i]);
            dist_and_lit_huffman_appearances[286 + dist] += 1;
            if ((dist + 1) > hdist) {
                hdist = dist + 1;
            }
        } else {
            dist_and_lit_huffman_appearances[s->in_buf[i]] += 1;
        }
    }
    // step 3: encode this efficiently with repetitions
    // move dist_and_lit_huffman_appearances so distances are right after literals
    memmove(dist_and_lit_huffman_appearances + hlit, dist_and_lit_huffman_appearances + 286, 32);
    int dist_and_lit_huffman_repetitions[286 + 32] = {0};
    int dalhr_i = 0;
    for (int x = 0; x < hlit + hdist + 258;) {
        // look for a repetition of the previous code length
        if (dist_and_lit_huffman_appearances[x] == 0) {
            int repeat_len = 0;
            while (dist_and_lit_huffman_appearances[x + repeat_len] == 0 && repeat_len < 138) {
                repeat_len += 1;
            }
            if (repeat_len >= 3) {
                // this is a repetition
                x += repeat_len;
                if (x > 0 && dist_and_lit_huffman_appearances[x - 1] == 0 && repeat_len <= 6) {
                    dist_and_lit_huffman_repetitions[dalhr_i++] = (16 << 7) | (repeat_len - 3);
                } else if (repeat_len <= 10) {
                    dist_and_lit_huffman_repetitions[dalhr_i++] = (17 << 7) | (repeat_len - 3);
                } else {
                    dist_and_lit_huffman_repetitions[dalhr_i++] = (18 << 7) | (repeat_len - 11);
                }
            } else {
                dist_and_lit_huffman_repetitions[dalhr_i++] = dist_and_lit_huffman_appearances[x++];
            }
        } else if (x > 0) {
            int prev = dist_and_lit_huffman_appearances[x - 1];
            int repeat_len = 0;
            while (dist_and_lit_huffman_appearances[x + repeat_len] == prev && repeat_len < 6) {
                repeat_len += 1;
            }
            if (repeat_len >= 3) {
                // this is a repetition
                x += repeat_len;
                dist_and_lit_huffman_repetitions[dalhr_i++] = (16 << 7) | (repeat_len - 3);
            } else {
                dist_and_lit_huffman_repetitions[dalhr_i++] = dist_and_lit_huffman_appearances[x++];
            }
        } else {
            dist_and_lit_huffman_repetitions[dalhr_i++] = dist_and_lit_huffman_appearances[x++];
        }
    }
    // step 4: construct huffman code...
}

// iterates from s->in_buf_index - i to s->in_buf_index - j, writes it using fixed huffman
static void fixed_huffman(int i, int j, struct state *s) {
    i = s->in_buf_index - i;
    j = s->in_buf_index - j;
    write_bits(0b01, 2, s); // header
    while (i < j) {
        int replen = s->repetition_len[i];
        if (replen == 0) {
            // write character as-is
            int lit = s->in_buf[i];
            if (lit <= 143) {
                // 8 bits - 00110000 through 10111111
                write_bits(0b0011000 + lit, 8, s);
            } else {
                // 9 bits - 110010000 through 111111111
                write_bits(0b110010000 + lit, 9, s);
            }
            i += 1;
        } else {
            // write this repetition
            int dist = s->repetition_dist[i];
            int lencode = 257 + binary_search(lengths_for_codes, 29, replen);
            if (lencode <= 279) {
                // 7 bits - 0000000 through 0010111
                write_bits(lencode - 256, 7, s);
            } else {
                // 8 bits - 11000000 through 11000111
                write_bits(0b11000000 + lencode - 280, 8, s);
            }
            // write extra bits based on lencode
            write_bits(replen - lengths_for_codes[lencode - 257], lengths_extra_bits[lencode], s);
            int distcode = 1 + binary_search(lengths_for_repeats, 30, dist);
            write_bits(distcode, 5, s);
            write_bits(dist - lengths_for_repeats[distcode - 1], lengths_extra_bits[distcode], s);
            i += replen;
        }
    }
    write_bits(0, 7, s); // 256
}

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
            // last chunk bit
            write_bits(1, 1, s);
            fixed_huffman(s->in_buf_index, 0, s);
            break;
        }
    }

    flush_bits(s);
    free(s);
    return 0;
}
