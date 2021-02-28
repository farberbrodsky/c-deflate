// Decompresses from src_fd to dest_fd
// Deflate sets a bit on the last block, so it stops itself
// Returns 0 if successful, otherwise an error code
int decompressor(int dest_fd, int src_fd);
