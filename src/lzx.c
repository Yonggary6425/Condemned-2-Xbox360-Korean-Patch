/* Xbox 360 LZX decoder for Monolith LTAR archive entries.
 * Written from the public LZX format description (Microsoft LZX / CAB). */
#include "lzx.h"
#include <string.h>
#include <stdlib.h>

#define WBITS 17
#define NSLOTS (WBITS * 2)
#define MAINLEN (256 + NSLOTS * 8)
#define LENLEN 249
#define ALNLEN 8
#define PRELEN 20
#define MAXBITS 16

typedef struct {
    const uint8_t *p, *end;
    uint32_t buf;
    int left;
} bits_t;

static void bits_init(bits_t *b, const uint8_t *p, size_t n) { b->p = p; b->end = p + n; b->buf = 0; b->left = 0; }

static void bits_fill(bits_t *b) {
    while (b->left <= 16) {
        uint32_t w = 0;
        if (b->p + 1 < b->end) { w = b->p[0] | ((uint32_t)b->p[1] << 8); b->p += 2; }
        else if (b->p < b->end) { w = b->p[0]; b->p = b->end; }
        b->buf |= w << (16 - b->left);
        b->left += 16;
    }
}

static uint32_t peekb(bits_t *b, int n) { bits_fill(b); return b->buf >> (32 - n); }
static void dropb(bits_t *b, int n) { b->buf <<= n; b->left -= n; }
static uint32_t readb(bits_t *b, int n) {
    uint32_t v;
    if (n == 0) return 0;
    if (n > 16) { v = readb(b, 16) << (n - 16); return v | readb(b, n - 16); }
    v = peekb(b, n); dropb(b, n); return v;
}

/* canonical Huffman decoder (counting method) */
typedef struct {
    uint16_t count[MAXBITS + 1];
    uint16_t sym[MAINLEN];
    int any;
} huff_t;

static int huff_build(huff_t *h, const uint8_t *lens, int n) {
    uint16_t offs[MAXBITS + 2];
    int i, left = 1;
    memset(h->count, 0, sizeof h->count);
    for (i = 0; i < n; i++) { if (lens[i] > MAXBITS) return -1; h->count[lens[i]]++; }
    h->any = (h->count[0] != n);
    h->count[0] = 0;
    for (i = 1; i <= MAXBITS; i++) { left <<= 1; left -= h->count[i]; if (left < 0) return -1; }
    offs[1] = 0;
    for (i = 1; i < MAXBITS; i++) offs[i + 1] = offs[i] + h->count[i];
    for (i = 0; i < n; i++) if (lens[i]) h->sym[offs[lens[i]]++] = (uint16_t)i;
    return 0;
}

static int huff_dec(bits_t *b, const huff_t *h) {
    int code = 0, first = 0, index = 0, len;
    uint32_t bitsv = peekb(b, MAXBITS);
    if (!h->any) return -1;
    for (len = 1; len <= MAXBITS; len++) {
        int cnt = h->count[len];
        code |= (bitsv >> (MAXBITS - len)) & 1;
        if (code - cnt < first) { dropb(b, len); return h->sym[index + (code - first)]; }
        index += cnt; first += cnt; first <<= 1; code <<= 1;
    }
    return -1;
}

static int read_lens(bits_t *b, uint8_t *lens, int first, int last) {
    uint8_t pl[PRELEN];
    huff_t pt;
    int x, z, r;
    for (x = 0; x < PRELEN; x++) pl[x] = (uint8_t)readb(b, 4);
    if (huff_build(&pt, pl, PRELEN)) return -1;
    for (x = first; x < last;) {
        z = huff_dec(b, &pt); if (z < 0) return -1;
        if (z == 17) { r = readb(b, 4) + 4; while (r-- && x < last) lens[x++] = 0; }
        else if (z == 18) { r = readb(b, 5) + 20; while (r-- && x < last) lens[x++] = 0; }
        else if (z == 19) {
            r = readb(b, 1) + 4; z = huff_dec(b, &pt); if (z < 0 || z > 16) return -1;
            z = lens[x] - z; if (z < 0) z += 17;
            while (r-- && x < last) lens[x++] = (uint8_t)z;
        } else { z = lens[x] - z; if (z < 0) z += 17; lens[x++] = (uint8_t)z; }
    }
    return 0;
}

static uint32_t pos_base[NSLOTS];
static uint8_t extra_bits[NSLOTS];
static int tables_ready = 0;
static void init_tables(void) {
    int i; uint32_t base = 0;
    for (i = 0; i < NSLOTS; i++) {
        extra_bits[i] = (uint8_t)(i < 4 ? 0 : ((i - 2) >> 1) > 17 ? 17 : ((i - 2) >> 1));
        pos_base[i] = base; base += 1u << extra_bits[i];
    }
    tables_ready = 1;
}

/* Decode one independently compressed chunk (sequence of frames). */
static int lzx_chunk(const uint8_t *body, size_t blen, uint8_t *out, size_t outlen) {
    static uint8_t mainl[MAINLEN], lenl[LENLEN], alnl[ALNLEN];
    static huff_t mt, lt, at;
    uint32_t R0 = 1, R1 = 1, R2 = 1;
    int btype = 0, first = 1;
    size_t remain = 0, o = 0, q = 0, frame = 0;
    if (!tables_ready) init_tables();
    memset(mainl, 0, sizeof mainl); memset(lenl, 0, sizeof lenl);
    while (q < blen && o < outlen) {
        size_t fl, fusz = 32768, target; bits_t b;
        if (body[q] == 0xFF) { if (q + 5 > blen) return -1; fusz = ((size_t)body[q+1] << 8) | body[q+2]; fl = ((size_t)body[q+3] << 8) | body[q+4]; q += 5; }
        else { if (q + 2 > blen) return -1; fl = ((size_t)body[q] << 8) | body[q+1]; q += 2; }
        if (q + fl > blen) return -1;
        bits_init(&b, body + q, fl);
        target = frame * 32768 + fusz; if (target > outlen) target = outlen;
        if (first) { if (readb(&b, 1)) return -2; /* Intel E8 translation: not used here */ first = 0; }
        while (o < target) {
            if (remain == 0) {
                btype = (int)readb(&b, 3);
                remain = readb(&b, 24);
                if (btype == 2) {
                    int i; for (i = 0; i < ALNLEN; i++) alnl[i] = (uint8_t)readb(&b, 3);
                    if (huff_build(&at, alnl, ALNLEN)) return -1;
                }
                if (btype == 1 || btype == 2) {
                    if (read_lens(&b, mainl, 0, 256) || read_lens(&b, mainl, 256, MAINLEN)) return -1;
                    if (huff_build(&mt, mainl, MAINLEN)) return -1;
                    if (read_lens(&b, lenl, 0, LENLEN)) return -1;
                    if (huff_build(&lt, lenl, LENLEN)) return -1;
                } else if (btype == 3) {
                    /* align to 16 bits, then R0..R2 as raw little-endian dwords */
                    const uint8_t *p;
                    if (b.left == 0) { bits_fill(&b); }
                    /* drop buffered bits back to byte position */
                    p = b.p - ((b.left / 16) * 2);
                    if (b.left % 16 == 0) { /* whole words buffered; we must skip one word */ p += 2; }
                    else { p = b.p - ((b.left / 16) * 2); }
                    if (p + 12 > b.end) return -1;
                    R0 = p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
                    R1 = p[4] | p[5] << 8 | p[6] << 16 | (uint32_t)p[7] << 24;
                    R2 = p[8] | p[9] << 8 | p[10] << 16 | (uint32_t)p[11] << 24;
                    b.p = p + 12; b.buf = 0; b.left = 0;
                } else return -1;
            }
            if (btype == 3) {
                size_t n = remain, avail = (size_t)(b.end - b.p);
                if (n > target - o) n = target - o;
                if (n > avail) return -1;
                memcpy(out + o, b.p, n); b.p += n; o += n; remain -= n;
                if (remain == 0 && (b.p < b.end) && (((size_t)(b.p - (body + q))) & 1)) b.p++;
                continue;
            }
            while (remain > 0 && o < target) {
                int m = huff_dec(&b, &mt);
                if (m < 0) return -1;
                if (m < 256) { out[o++] = (uint8_t)m; remain--; continue; }
                {
                    uint32_t len, off, slot, ex;
                    m -= 256;
                    len = m & 7;
                    if (len == 7) { int f = huff_dec(&b, &lt); if (f < 0) return -1; len += f; }
                    len += 2;
                    slot = m >> 3;
                    if (slot == 0) off = R0;
                    else if (slot == 1) { off = R1; R1 = R0; R0 = off; }
                    else if (slot == 2) { off = R2; R2 = R0; R0 = off; }
                    else {
                        ex = extra_bits[slot];
                        off = pos_base[slot] - 2;
                        if (btype == 2) {
                            if (ex > 3) { off += readb(&b, ex - 3) << 3; { int a = huff_dec(&b, &at); if (a < 0) return -1; off += a; } }
                            else if (ex == 3) { int a = huff_dec(&b, &at); if (a < 0) return -1; off += a; }
                            else if (ex > 0) off += readb(&b, ex);
                            else off = 1;
                        } else {
                            off += readb(&b, ex);
                        }
                        R2 = R1; R1 = R0; R0 = off;
                    }
                    if (off > o || len > remain || o + len > outlen) return -1;
                    { size_t s = o - off; uint32_t k; for (k = 0; k < len; k++) out[o + k] = out[s + k]; }
                    o += len; remain -= len;
                }
            }
        }
        q += fl; frame++;
    }
    return o == outlen ? 0 : -1;
}

int lzx_decode_entry(const uint8_t *c, size_t clen, uint8_t *out, size_t usize) {
    size_t o = 0, done = 0;
    while (done < usize) {
        uint32_t hdr, cs; size_t n = usize - done;
        if (o + 4 > clen) return -1;
        hdr = (uint32_t)c[o] << 24 | c[o+1] << 16 | c[o+2] << 8 | c[o+3];
        if (n > 0x10000) n = 0x10000;
        if (hdr & 0xF0000000u) {
            cs = hdr & 0x0FFFFFFF;
            if (o + 4 + cs > clen || cs > usize - done) return -1;
            memcpy(out + done, c + o + 4, cs); done += cs;
        } else {
            cs = hdr;
            if (o + 4 + cs > clen) return -1;
            if (lzx_chunk(c + o + 4, cs, out + done, n)) return -1;
            done += n;
        }
        o = (o + 4 + cs + 3) & ~(size_t)3;
    }
    return 0;
}
