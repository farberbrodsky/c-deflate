import zlib
import sys

compress = sys.stdin.buffer.read()

def deflate(data, compresslevel=9):
    compress = zlib.compressobj(
        compresslevel, zlib.DEFLATED, -zlib.MAX_WBITS, zlib.DEF_MEM_LEVEL, 0
    )
    deflated = compress.compress(data)
    deflated += compress.flush()
    return deflated

sys.stdout.buffer.write(deflate(compress))
