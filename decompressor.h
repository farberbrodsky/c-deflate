#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <endian.h>
// Decompresses from src_fd to dest_fd
// Deflate sets a bit on the last block, so it stops itself
// Returns 0 if successful, otherwise an error code
#define ERR_INVALID_DEFLATE 1
int decompressor(int dest_fd, int src_fd);
