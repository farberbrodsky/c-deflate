#ifndef GUARD_1c956377_42d6_4b64_a3fd_9d599ef54024
#define GUARD_1c956377_42d6_4b64_a3fd_9d599ef54024
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <setjmp.h>
#include <unistd.h>
#include <stdint.h>
#include <endian.h>
#include <stdbool.h>
#define MAX_CODEBITS 15   // a huffman code can't be more than 15 bits
#define MAX_CODES    286  // there are only 286 codes encoded
#include "compressor.h"
#include "decompressor.h"
#endif
