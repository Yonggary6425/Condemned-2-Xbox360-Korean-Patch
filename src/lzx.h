/* Xbox 360 LZX (XMemCompress style) decoder used by Monolith LTAR archives.
 * Each 64 KB chunk is compressed independently with a 128 KB window and is
 * split into 32 KB frames. Written from the public LZX format description. */
#ifndef C2_LZX_H
#define C2_LZX_H
#include <stddef.h>
#include <stdint.h>

/* Decode one LTAR entry (sequence of chunks) into out[0..usize).
 * Returns 0 on success, nonzero on a format error. */
int lzx_decode_entry(const uint8_t *src, size_t srclen, uint8_t *out, size_t usize);

#endif
