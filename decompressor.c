#include "decompressor.h"

typedef uint_fast8_t uf8;
typedef uint_fast16_t uf16;

// first bit  (0b01) - the real bit
// second bit (0b10) - whether this is the end
inline static uf8 read_bit(int src_fd, size_t *bit_index, uf8 *curr_byte) {
    uf8 mod = 7 - (*bit_index) % 8;
    uf8 result = ((*curr_byte) & 0b10000000) != 0;
    *curr_byte = (*curr_byte) << 1;
    ++(*bit_index);
    if (mod == 0) {
        // read from next byte
        if (read(src_fd, curr_byte, 1) == 0) {
            // couldn't read anything
            return result | 0b10;
        }
    }
    return result;
}

static uf8 read_byte(int src_fd, size_t *bit_index, uf8 *curr_byte) {
    uf8 result = 0;
    for (uf8 i = 0; i < 8; ++i) {
        result = result << 1;
        result |= read_bit(src_fd, bit_index, curr_byte) & 0b01;
    }
    return result;
}

int decompressor(int dest_fd, int src_fd) {
    size_t bit_index = 0;
    uf8 curr_byte;
    read(src_fd, &curr_byte, 1);

    uf8 final_block = 0;
    while (!final_block) {
        // first bit - whether this is the final block
        final_block = read_bit(src_fd, &bit_index, &curr_byte);
        // read the next two bytes, which are block type
        uf8 type_1 = read_bit(src_fd, &bit_index, &curr_byte) & 0b01;
        uf8 type_2 = read_bit(src_fd, &bit_index, &curr_byte) & 0b01;
        if (!type_1 && !type_2) {
            // no compression, read the amount of bytes in this block
            uf8 first_byte  = read_byte(src_fd, &bit_index, &curr_byte);
            uf8 second_byte = read_byte(src_fd, &bit_index, &curr_byte);
            uf16 len = (first_byte << 8) + second_byte;
            len = be16toh(len); // read from big endian
            // TODO: check that nlen is actually the complement to 1
            read_byte(src_fd, &bit_index, &curr_byte);
            read_byte(src_fd, &bit_index, &curr_byte);
            // now read len bytes
            printf("non-compressed, %d bytes block\n", (int)len);
            for (uint_fast32_t i = 0; i < len; ++i) {
                uf8 byte = read_byte(src_fd, &bit_index, &curr_byte);
                write(dest_fd, &byte, 1);
            }
        } else if (!type_1 && type_2) {
            // fixed Huffman codes
            printf("fixed Hufffman codes\n");
        } else if (type_1 && !type_2) {
            // dynamic Huffman codes
            printf("dynamic Hufffman codes\n");
        } else {
            // reserved (error)
            return ERR_INVALID_DEFLATE;
        }
    }
    return 0;
}
