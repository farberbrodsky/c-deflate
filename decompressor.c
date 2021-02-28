#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <fcntl.h>   // required for open

typedef uint_fast8_t uf8;

// first bit  (0b01) - the real bit
// second bit (0b10) - whether this is the end
inline static uf8 read_bit(int src_fd, size_t *bit_index, uf8 *curr_byte) {
    uf8 mod = 7 - (*bit_index) % 8;
    uf8 result = *curr_byte & (1 << mod);
    ++(*bit_index);
    if (mod == 0) {
        // read from next byte
        if (read(src_fd, curr_byte, 1) == 0) {
            // couldn't read anything
            return result | 0b10;
        }
    }
    return !(result == 0);
}

int decompressor(int dest_fd, int src_fd) {
    size_t bit_index = 0;
    uf8 curr_byte;
    read(src_fd, &curr_byte, 1);

    // just print the bits for now
    size_t i = 0;
    uf8 current_bit = 0;
    while (!(current_bit & 0b10)) {
        if (i != 0 && i % 8 == 0) {
            printf("\n");
        }
        current_bit = read_bit(src_fd, &bit_index, &curr_byte);
        printf("%d", (int)current_bit & 0b01);
        ++i;
    }
    return 0;
}
