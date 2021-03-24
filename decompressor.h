#include <stdio.h>
#include <setjmp.h>
#include <unistd.h>
#include <stdint.h>
#include <endian.h>
// Decompresses from src to dest
// Deflate sets a bit on the last block, so it stops itself
// Returns 0 if successful, otherwise an error code
#define ERR_INVALID_DEFLATE 1
#define DEFLATE_DEBUGGING
int decompressor(FILE *dest, FILE *src);
