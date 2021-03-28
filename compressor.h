#ifndef GUARD_bc949154_fd4c_4fd6_8196_8a128f28956b
#define GUARD_bc949154_fd4c_4fd6_8196_8a128f28956b
#include "deflate.h"
// Compresses from src to dest
// Returns 0 if successful, otherwise an error code
int compressor(FILE *dest, FILE *src);
#endif
