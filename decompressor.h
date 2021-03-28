#ifndef GUARD_8c157709_e8f5_4bf5_b86e_bf2be4b7d787
#define GUARD_8c157709_e8f5_4bf5_b86e_bf2be4b7d787
#include "deflate.h"
// Decompresses from src to dest
// Deflate sets a bit on the last block, so it stops itself
// Returns 0 if successful, otherwise an error code
#define ERR_INVALID_DEFLATE 1
int decompressor(FILE *dest, FILE *src);
#endif
