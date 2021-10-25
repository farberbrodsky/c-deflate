# c-deflate

A decompressor for Deflate, according to https://tools.ietf.org/html/rfc1951.

The library exports the function `decompressor`, which receives two files. It reads from the second file and writes to the first file (as simple as it gets!)


Example usage:
```c
decompressor(stdout, stdin);
```
