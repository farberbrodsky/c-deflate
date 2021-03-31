#include "deflate.h"

int main(int argc, char **argv) {
    if (!strcmp(argv[1], "compress")) {
        return compressor(stdout, stdin);
    }
    return decompressor(stdout, stdin);
}
