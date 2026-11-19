/*
 * Condemned 2: Bloodshot (Xbox 360) Korean patcher
 *
 * A simple port of the PS3 Korean fan patch to the Xbox 360 version.
 * The translation and Korean fonts are entirely the work of the original
 * PS3 Korean patch author: https://cafe.naver.com/hansicgu/34493
 *
 * The patcher reads the user's own original ISO, decodes the string
 * databases and interface bundle from Layer01.Arch02, applies the embedded
 * Korean data and writes a new "<name> [KOR].iso". The original ISO is never
 * modified and no game data is embedded in this program.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include "sha256.h"
#include "lzx.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
typedef wchar_t pchar;
#define P(s) L##s
#define pstrlen wcslen
#define pstrcpy wcscpy
#define pstrcat wcscat
#define pstrrchr wcsrchr
static FILE *popen_file(const pchar *p, const pchar *m) { return _wfopen(p, m); }
#define fseek64 _fseeki64
#define ftell64 _ftelli64
#else
#include <unistd.h>
#include <sys/statvfs.h>
typedef char pchar;
#define P(s) s
#define pstrlen strlen
#define pstrcpy strcpy
#define pstrcat strcat
#define pstrrchr strrchr
static FILE *popen_file(const pchar *p, const pchar *m) { return fopen(p, m); }
#define fseek64 fseeko
#define ftell64 ftello
#endif

#define APP_VERSION "1.0"

extern const uint8_t c2_blob[];
extern const uint8_t c2_blob_end[];

/* ------------------------------------------------------------------ output */
static void say(const char *fmt, ...) {
    char buf[4096];
    va_list ap;
    va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
#ifdef _WIN32
    {
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode, w;
        if (GetConsoleMode(h, &mode)) {
            wchar_t wb[4096];
            int n = MultiByteToWideChar(CP_UTF8, 0, buf, -1, wb, 4096);
            if (n > 0) WriteConsoleW(h, wb, (DWORD)(n - 1), &w, NULL);
        } else {
            WriteFile(h, buf, (DWORD)strlen(buf), &w, NULL);
        }
    }
#else
    fputs(buf, stdout); fflush(stdout);
#endif
}

static int g_interactive = 1;
static void wait_exit(void) {
    if (!g_interactive) return;
    say("\nEnter 키를 누르면 창이 닫힙니다.");
    { int c; while ((c = getchar()) != '\n' && c != EOF) {} }
}

static FILE *g_out;
static const pchar *g_tmp;
static void remove_file(const pchar *p);
static void fail(const char *fmt, ...) {
    char buf[2048];
    va_list ap;
    va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    say("\n[오류] %s\n", buf);
    if (g_out) { fclose(g_out); g_out = NULL; }
    if (g_tmp) { remove_file(g_tmp); g_tmp = NULL; }
    wait_exit();
    exit(1);
}

/* --------------------------------------------------------------- byte utils */
static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint64_t le64(const uint8_t *p) { return le32(p) | (uint64_t)le32(p + 4) << 32; }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static uint64_t be64(const uint8_t *p) { return (uint64_t)be32(p) << 32 | be32(p + 4); }
static void put_le32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

static void *xmalloc(size_t n) { void *p = malloc(n ? n : 1); if (!p) fail("메모리가 부족합니다."); return p; }

/* ------------------------------------------------------------------ file io */
static void rd(FILE *f, uint64_t off, void *buf, size_t n) {
    if (fseek64(f, (int64_t)off, SEEK_SET) != 0 || fread(buf, 1, n, f) != n) fail("ISO를 읽는 중 문제가 생겼습니다 (위치 %llu).", (unsigned long long)off);
}
static void wr(FILE *f, const void *buf, size_t n) {
    if (n && fwrite(buf, 1, n, f) != n) fail("새 ISO를 쓰는 중 문제가 생겼습니다. 디스크 여유 공간을 확인해 주세요.");
}
static uint64_t fsize(FILE *f) { fseek64(f, 0, SEEK_END); return (uint64_t)ftell64(f); }

/* ------------------------------------------------------------- patch data */
typedef struct {
    uint64_t orig_size;
    uint32_t toc_len, app_len;
    const uint8_t *sha_orig_toc, *sha_kor_toc, *sha_app;
    uint32_t nsrc;
    struct { uint32_t ent; const uint8_t *sha; uint32_t dlen; const uint8_t *delta; } src[4];
    uint32_t nout;
    struct { uint32_t ent, src; const uint8_t *rec; } out[16];
} patch_t;

static void load_patch(patch_t *pt) {
    const uint8_t *p = c2_blob, *end = c2_blob_end;
    uint32_t i;
    if ((size_t)(end - p) < 64 || memcmp(p, "C2KP", 4)) fail("패처 내부 데이터가 손상되었습니다.");
    p += 4;
    pt->orig_size = le64(p); pt->toc_len = le32(p + 8); pt->app_len = le32(p + 12); p += 16;
    pt->sha_orig_toc = p; pt->sha_kor_toc = p + 32; pt->sha_app = p + 64; p += 96;
    pt->nsrc = le32(p); p += 4;
    if (pt->nsrc > 4) fail("패처 내부 데이터가 손상되었습니다.");
    for (i = 0; i < pt->nsrc; i++) { pt->src[i].ent = le32(p); pt->src[i].sha = p + 4; pt->src[i].dlen = le32(p + 36); p += 40; }
    pt->nout = le32(p); p += 4;
    if (pt->nout > 16) fail("패처 내부 데이터가 손상되었습니다.");
    for (i = 0; i < pt->nout; i++) { pt->out[i].ent = le32(p); pt->out[i].src = le32(p + 4); pt->out[i].rec = p + 8; p += 40; }
    for (i = 0; i < pt->nsrc; i++) { pt->src[i].delta = p; p += pt->src[i].dlen; }
    if (p > end) fail("패처 내부 데이터가 손상되었습니다.");
}

/* apply a copy/insert delta; returns malloc'd buffer */
static uint8_t *apply_delta(const uint8_t *src, size_t slen, const uint8_t *d, size_t dlen, size_t *olen) {
    size_t cap = 0, n = 0, o = 0;
    uint8_t *out;
    while (o < dlen) {
        if (d[o] == 'C') { cap += le32(d + o + 5); o += 9; }
        else if (d[o] == 'I') { uint32_t L = le32(d + o + 1); cap += L; o += 5 + L; }
        else fail("패처 내부 데이터가 손상되었습니다.");
    }
    out = (uint8_t *)xmalloc(cap);
    for (o = 0; o < dlen;) {
        if (d[o] == 'C') {
            uint32_t s = le32(d + o + 1), L = le32(d + o + 5);
            if ((uint64_t)s + L > slen) fail("패처 내부 데이터가 손상되었습니다.");
            memcpy(out + n, src + s, L); n += L; o += 9;
        } else {
            uint32_t L = le32(d + o + 1);
            memcpy(out + n, d + o + 5, L); n += L; o += 5 + L;
        }
    }
    *olen = n;
    return out;
}

/* ------------------------------------------------------------------ XDVDFS */
#define SECT 2048ull
typedef struct {
    uint64_t pos;       /* absolute ISO offset of the directory entry */
    uint32_t sector, size;
    int isdir;
    char path[512];
} dirent_t;

typedef struct { dirent_t *v; size_t n, cap; } dlist_t;

static void dl_push(dlist_t *l, const dirent_t *e) {
    if (l->n == l->cap) { l->cap = l->cap ? l->cap * 2 : 256; l->v = (dirent_t *)realloc(l->v, l->cap * sizeof *l->v); if (!l->v) fail("메모리가 부족합니다."); }
    l->v[l->n++] = *e;
}

static void walk(FILE *f, uint64_t part, uint32_t sector, uint32_t size, const char *prefix, dlist_t *out, int depth) {
    uint8_t *dir;
    uint32_t o = 0;
    if (size == 0) return;
    if (depth > 32 || size > 64u * 1024 * 1024) fail("ISO 디렉터리 구조를 읽을 수 없습니다.");
    dir = (uint8_t *)xmalloc(size);
    rd(f, part + sector * SECT, dir, size);
    while (o + 14 <= size) {
        uint16_t l = le16(dir + o);
        uint8_t nl = dir[o + 13];
        dirent_t e;
        if (l == 0xFFFF || nl == 0) { o = (o - (o % 2048)) + 2048; continue; }
        if (o + 14 + nl > size) break;
        memset(&e, 0, sizeof e);
        e.pos = part + sector * SECT + o;
        e.sector = le32(dir + o + 4);
        e.size = le32(dir + o + 8);
        e.isdir = (dir[o + 12] & 0x10) != 0;
        snprintf(e.path, sizeof e.path, "%s%s%.*s", prefix, prefix[0] ? "\\" : "", nl, (const char *)dir + o + 14);
        dl_push(out, &e);
        if (e.isdir) walk(f, part, e.sector, e.size, e.path, out, depth + 1);
        o += 14 + nl;
        o = (o + 3) & ~3u;
    }
    free(dir);
}

static int find_partition(FILE *f, uint64_t isz, uint64_t *part, uint32_t *rs, uint32_t *rz) {
    static const uint64_t cand[] = {0, 0xFD90000ull, 0x2080000ull, 0x18300000ull};
    size_t i;
    for (i = 0; i < 4; i++) {
        uint8_t b[28];
        if (cand[i] + 0x10000 + 28 > isz) continue;
        rd(f, cand[i] + 0x10000, b, 28);
        if (!memcmp(b, "MICROSOFT*XBOX*MEDIA", 20)) { *part = cand[i]; *rs = le32(b + 20); *rz = le32(b + 24); return 1; }
    }
    return 0;
}

static int ieq(const char *a, const char *b) {
    for (; *a && *b; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return 0;
    }
    return *a == *b;
}

/* ------------------------------------------------------------- copy helper */
static uint8_t *g_buf;
#define BUFSZ (8u * 1024 * 1024)
static uint64_t g_done, g_total;
static int g_lastpct = -1;

static void progress(uint64_t add) {
    int pct;
    g_done += add;
    pct = g_total ? (int)(g_done * 100 / g_total) : 100;
    if (pct != g_lastpct) { g_lastpct = pct; say("\r    진행: %3d%%", pct); }
}

static void copy_range(FILE *in, FILE *out, uint64_t off, uint64_t len) {
    if (len == 0) return;
    if (fseek64(in, (int64_t)off, SEEK_SET) != 0) fail("ISO를 읽는 중 문제가 생겼습니다.");
    while (len) {
        size_t n = len > BUFSZ ? BUFSZ : (size_t)len;
        if (fread(g_buf, 1, n, in) != n) fail("ISO를 읽는 중 문제가 생겼습니다.");
        wr(out, g_buf, n);
        len -= n;
        progress(n);
    }
}

static void zeros(FILE *out, uint64_t len) {
    memset(g_buf, 0, BUFSZ);
    while (len) { size_t n = len > BUFSZ ? BUFSZ : (size_t)len; wr(out, g_buf, n); len -= n; }
}

static uint64_t map_off(uint64_t off, uint64_t ins, uint64_t shift) { return off >= ins ? off + shift : off; }

/* ------------------------------------------------------------- free space */
static int enough_space(const pchar *outpath, uint64_t need) {
#ifdef _WIN32
    wchar_t dir[1024];
    ULARGE_INTEGER freeb;
    wchar_t *s;
    wcsncpy(dir, outpath, 1023); dir[1023] = 0;
    s = wcsrchr(dir, L'\\'); if (!s) s = wcsrchr(dir, L'/');
    if (s) s[1] = 0; else wcscpy(dir, L".\\");
    if (!GetDiskFreeSpaceExW(dir, &freeb, NULL, NULL)) return 1;
    return freeb.QuadPart >= need;
#else
    char dir[1024]; struct statvfs v; char *s;
    strncpy(dir, outpath, 1023); dir[1023] = 0;
    s = strrchr(dir, '/'); if (s) s[1] = 0; else strcpy(dir, ".");
    if (statvfs(dir, &v)) return 1;
    return (uint64_t)v.f_bavail * v.f_frsize >= need;
#endif
}

static int file_exists(const pchar *p) { FILE *f = popen_file(p, P("rb")); if (f) { fclose(f); return 1; } return 0; }

static void remove_file(const pchar *p) {
#ifdef _WIN32
    DeleteFileW(p);
#else
    remove(p);
#endif
}

static int rename_file(const pchar *a, const pchar *b) {
#ifdef _WIN32
    return MoveFileW(a, b) ? 0 : -1;
#else
    return rename(a, b);
#endif
}

/* ------------------------------------------------------------------- main */
static int run(const pchar *iso) {
    patch_t pt;
    FILE *f, *o;
    uint64_t isz, part, l01abs, oldalloc, newalloc, shift, ins;
    uint32_t rs, rz, delta_sect, i, j;
    dlist_t dl = {0, 0, 0};
    dirent_t *l01 = NULL;
    uint8_t *toc, *newtoc, *app, h[32];
    uint8_t *srcdata[4] = {0};
    size_t srclen[4] = {0};
    size_t app_n = 0;
    pchar outp[1100], tmpp[1100];

    load_patch(&pt);

    /* output names */
    if (pstrlen(iso) > 1000) fail("파일 경로가 너무 깁니다.");
    pstrcpy(outp, iso);
    {
        pchar *dot = pstrrchr(outp, P('.'));
        pchar *sl1 = pstrrchr(outp, P('\\')), *sl2 = pstrrchr(outp, P('/'));
        pchar *sl = sl1 > sl2 ? sl1 : sl2;
        if (dot && (!sl || dot > sl)) *dot = 0;
        pstrcat(outp, P(" [KOR].iso"));
    }
    pstrcpy(tmpp, outp); pstrcat(tmpp, P(".part"));

    say("[1/5] 원본 ISO 확인\n");
    f = popen_file(iso, P("rb"));
    if (!f) fail("ISO 파일을 열 수 없습니다.");
    isz = fsize(f);
    if (!find_partition(f, isz, &part, &rs, &rz)) fail("Xbox 360 ISO가 아닙니다 (XDVDFS 헤더를 찾지 못했습니다).");
    walk(f, part, rs, rz, "", &dl, 0);
    for (i = 0; i < dl.n; i++) if (!dl.v[i].isdir && ieq(dl.v[i].path, "Layer01.Arch02")) l01 = &dl.v[i];
    if (!l01) fail("Layer01.Arch02 파일이 없습니다. Condemned 2: Bloodshot (USA, Europe) ISO가 맞는지 확인해 주세요.");
    l01abs = part + l01->sector * SECT;
    toc = (uint8_t *)xmalloc(pt.toc_len);
    rd(f, l01abs, toc, pt.toc_len);
    sha256(toc, pt.toc_len, h);
    if (l01->size == pt.orig_size + pt.app_len && !memcmp(h, pt.sha_kor_toc, 32))
        fail("이미 한글패치가 적용된 ISO입니다.");
    if (l01->size != pt.orig_size || memcmp(h, pt.sha_orig_toc, 32))
        fail("지원하지 않는 ISO입니다. Condemned 2: Bloodshot (USA, Europe) 원본 ISO만 지원합니다.");
    say("    Condemned 2: Bloodshot (USA, Europe) 원본 확인\n");

    say("[2/5] 원본 데이터 읽기\n");
    for (i = 0; i < pt.nsrc; i++) {
        const uint8_t *rec = toc + pt.src[i].ent;
        uint64_t off = be64(rec + 4), csz = be64(rec + 12), usz = be64(rec + 20);
        uint8_t *c;
        if (off + csz > pt.orig_size || usz > 64u * 1024 * 1024 || csz > 64u * 1024 * 1024) fail("원본 데이터 구조가 예상과 다릅니다.");
        c = (uint8_t *)xmalloc((size_t)csz);
        rd(f, l01abs + off, c, (size_t)csz);
        srcdata[i] = (uint8_t *)xmalloc((size_t)usz); srclen[i] = (size_t)usz;
        if (lzx_decode_entry(c, (size_t)csz, srcdata[i], (size_t)usz)) fail("원본 데이터 압축 해제에 실패했습니다.");
        free(c);
        sha256(srcdata[i], srclen[i], h);
        if (memcmp(h, pt.src[i].sha, 32)) fail("원본 데이터가 예상과 다릅니다 (해시 불일치).");
    }

    say("[3/5] 한글 데이터 조립\n");
    app = (uint8_t *)xmalloc(pt.app_len);
    newtoc = (uint8_t *)xmalloc(pt.toc_len);
    memcpy(newtoc, toc, pt.toc_len);
    for (i = 0; i < pt.nout; i++) {
        uint32_t si = pt.out[i].src;
        uint64_t off = be64(pt.out[i].rec + 4), csz = be64(pt.out[i].rec + 12), usz = be64(pt.out[i].rec + 20);
        size_t nl, p = 0;
        uint8_t *d;
        if (si >= pt.nsrc || pt.out[i].ent + 32 > pt.toc_len) fail("패처 내부 데이터가 손상되었습니다.");
        if (off != pt.orig_size + app_n) fail("패처 내부 데이터가 손상되었습니다.");
        d = apply_delta(srcdata[si], srclen[si], pt.src[si].delta, pt.src[si].dlen, &nl);
        if (nl != usz) fail("한글 데이터 조립에 실패했습니다.");
        /* stored (uncompressed) chunks of up to 64 KB, 4-byte aligned */
        while (p < nl) {
            size_t n = nl - p > 0x10000 ? 0x10000 : nl - p;
            uint32_t hdr = 0xF0000000u | (uint32_t)n;
            if (app_n + 4 + n + 3 > pt.app_len) fail("한글 데이터 조립에 실패했습니다.");
            app[app_n] = (uint8_t)(hdr >> 24); app[app_n+1] = (uint8_t)(hdr >> 16); app[app_n+2] = (uint8_t)(hdr >> 8); app[app_n+3] = (uint8_t)hdr;
            memcpy(app + app_n + 4, d + p, n);
            app_n += 4 + n; p += n;
            while (app_n & 3) app[app_n++] = 'X';
        }
        if (pt.orig_size + app_n != off + csz) fail("한글 데이터 조립에 실패했습니다.");
        free(d);
        memcpy(newtoc + pt.out[i].ent, pt.out[i].rec, 32);
    }
    if (app_n != pt.app_len) fail("한글 데이터 조립에 실패했습니다.");
    sha256(app, app_n, h);
    if (memcmp(h, pt.sha_app, 32)) fail("한글 데이터 검증에 실패했습니다.");
    sha256(newtoc, pt.toc_len, h);
    if (memcmp(h, pt.sha_kor_toc, 32)) fail("한글 목차 검증에 실패했습니다.");
    for (i = 0; i < pt.nsrc; i++) free(srcdata[i]);

    /* layout */
    oldalloc = (pt.orig_size + SECT - 1) / SECT;
    newalloc = (pt.orig_size + pt.app_len + SECT - 1) / SECT;
    delta_sect = (uint32_t)(newalloc - oldalloc);
    shift = (uint64_t)delta_sect * SECT;
    ins = l01abs + oldalloc * SECT;
    if (ins > isz) fail("ISO 파일이 잘려 있습니다.");
    for (i = 0; i < dl.n; i++) {
        dirent_t *e = &dl.v[i];
        if (e == l01) continue;
        if (e->sector > l01->sector && e->sector < l01->sector + oldalloc) fail("ISO 배치가 예상과 다릅니다.");
    }

    say("[4/5] 새 ISO 만들기 (약 %.1f GB)\n", (double)(isz + shift) / 1e9);
    if (file_exists(outp)) fail("결과 파일이 이미 있습니다. 지우거나 다른 곳으로 옮긴 뒤 다시 실행해 주세요.");
    if (!enough_space(outp, isz + shift + 64ull * 1024 * 1024)) fail("디스크 여유 공간이 부족합니다. 약 %.1f GB가 필요합니다.", (double)(isz + shift) / 1e9);
    o = popen_file(tmpp, P("wb"));
    if (!o) fail("결과 파일을 만들 수 없습니다.");
    g_out = o; g_tmp = tmpp;
    g_buf = (uint8_t *)xmalloc(BUFSZ);
    g_total = isz; g_done = 0;
    copy_range(f, o, 0, l01abs);
    wr(o, newtoc, pt.toc_len); progress(pt.toc_len);
    copy_range(f, o, l01abs + pt.toc_len, pt.orig_size - pt.toc_len);
    wr(o, app, app_n);
    zeros(o, l01abs + newalloc * SECT - (l01abs + pt.orig_size + app_n));
    progress(ins - (l01abs + pt.orig_size));
    copy_range(f, o, ins, isz - ins);
    say("\n");

    /* fix directory entries */
    for (i = 0; i < dl.n; i++) {
        dirent_t *e = &dl.v[i];
        uint8_t b[8];
        uint64_t at = map_off(e->pos, ins, shift);
        uint32_t sec = e->sector, sz = e->size;
        if (e != l01 && sec <= l01->sector) continue;
        if (e != l01) sec += delta_sect;
        if (e == l01) sz = (uint32_t)(pt.orig_size + pt.app_len);
        put_le32(b, sec); put_le32(b + 4, sz);
        if (fseek64(o, (int64_t)(at + 4), SEEK_SET) != 0) fail("새 ISO를 쓰는 중 문제가 생겼습니다.");
        wr(o, b, 8);
    }
    if (rs > l01->sector) {
        uint8_t b[4]; put_le32(b, rs + delta_sect);
        fseek64(o, (int64_t)(part + 0x10000 + 20), SEEK_SET); wr(o, b, 4);
    }
    g_out = NULL;
    if (fclose(o)) fail("새 ISO를 쓰는 중 문제가 생겼습니다.");

    say("[5/5] 새 ISO 검증\n");
    o = popen_file(tmpp, P("rb"));
    if (!o) fail("새 ISO를 열 수 없습니다.");
    g_out = o;
    {
        dlist_t nl = {0, 0, 0};
        uint64_t np; uint32_t nrs, nrz;
        uint8_t *a = (uint8_t *)xmalloc(65536), *b = (uint8_t *)xmalloc(65536);
        if (fsize(o) != isz + shift) fail("검증 실패: 새 ISO 크기가 맞지 않습니다.");
        if (!find_partition(o, isz + shift, &np, &nrs, &nrz) || np != part) fail("검증 실패: 새 ISO 헤더.");
        walk(o, np, nrs, nrz, "", &nl, 0);
        if (nl.n != dl.n) fail("검증 실패: 파일 목록이 다릅니다.");
        g_total = dl.n; g_done = 0; g_lastpct = -1;
        for (i = 0; i < dl.n; i++) {
            dirent_t *e = &dl.v[i], *n = NULL;
            for (j = 0; j < nl.n; j++) if (!strcmp(nl.v[j].path, e->path)) { n = &nl.v[j]; break; }
            if (!n || n->isdir != e->isdir) fail("검증 실패: %s", e->path);
            if (e == l01) {
                uint8_t *t;
                if (n->size != pt.orig_size + pt.app_len || n->sector != e->sector) fail("검증 실패: Layer01.Arch02");
                t = (uint8_t *)xmalloc(pt.app_len > pt.toc_len ? pt.app_len : pt.toc_len);
                rd(o, np + n->sector * SECT, t, pt.toc_len); sha256(t, pt.toc_len, h);
                if (memcmp(h, pt.sha_kor_toc, 32)) fail("검증 실패: 한글 목차");
                rd(o, np + n->sector * SECT + pt.orig_size, t, pt.app_len); sha256(t, pt.app_len, h);
                if (memcmp(h, pt.sha_app, 32)) fail("검증 실패: 한글 데이터");
                free(t);
                /* compare original data right after the table of contents and at the end */
                rd(f, l01abs + pt.toc_len, a, 65536); rd(o, np + n->sector * SECT + pt.toc_len, b, 65536);
                if (memcmp(a, b, 65536)) fail("검증 실패: Layer01.Arch02 본문");
                rd(f, l01abs + pt.orig_size - 65536, a, 65536); rd(o, np + n->sector * SECT + pt.orig_size - 65536, b, 65536);
                if (memcmp(a, b, 65536)) fail("검증 실패: Layer01.Arch02 끝부분");
            } else {
                uint32_t want = e->sector > l01->sector ? e->sector + delta_sect : e->sector;
                if (n->sector != want || n->size != e->size) fail("검증 실패: %s", e->path);
                if (!e->isdir && e->size) {
                    size_t k = e->size < 65536 ? e->size : 65536;
                    rd(f, part + e->sector * SECT, a, k); rd(o, np + n->sector * SECT, b, k);
                    if (memcmp(a, b, k)) fail("검증 실패: %s", e->path);
                    rd(f, part + e->sector * SECT + e->size - k, a, k); rd(o, np + n->sector * SECT + n->size - k, b, k);
                    if (memcmp(a, b, k)) fail("검증 실패: %s", e->path);
                }
            }
            progress(1);
        }
        say("\n");
        free(a); free(b); free(nl.v);
    }
    g_out = NULL; fclose(o);
    fclose(f);
    if (rename_file(tmpp, outp)) fail("결과 파일 이름을 바꿀 수 없습니다.");
    g_tmp = NULL;
    free(dl.v); free(toc); free(newtoc); free(app); free(g_buf);
    return 0;
}

static void banner(void) {
    say("==============================================================\n");
    say(" Condemned 2: Bloodshot (Xbox 360) 한글패치 v" APP_VERSION "\n");
    say(" - PS3 한글패치를 Xbox 360판으로 옮긴 단순 포팅판 -\n");
    say("==============================================================\n");
    say(" 이 패치는 PS3판 한글패치를 Xbox 360판에 그대로 옮긴 것입니다.\n");
    say(" 번역과 한글 폰트는 모두 PS3 한글패치 원 제작자의 작업물입니다.\n");
    say("   원본 PS3 한글패치: https://cafe.naver.com/hansicgu/34493\n");
    say(" 360 이식(포팅): 용가리 (github.com/Yonggary6425)\n");
    say(" 비공식·비상업적 팬 패치이며, 원본 ISO는 수정하지 않습니다.\n");
    say("==============================================================\n\n");
}

#ifdef _WIN32
static void strip_path(wchar_t *s) {
    size_t n = wcslen(s);
    while (n && (s[n-1] == L'\r' || s[n-1] == L'\n' || s[n-1] == L' ')) s[--n] = 0;
    if (n >= 2 && s[0] == L'"' && s[n-1] == L'"') { memmove(s, s + 1, (n - 2) * sizeof(wchar_t)); s[n-2] = 0; }
}
int wmain(int argc, wchar_t **argv) {
    wchar_t path[1024];
    SetConsoleOutputCP(CP_UTF8);
    banner();
    if (argc >= 2) { wcsncpy(path, argv[1], 1023); path[1023] = 0; }
    else {
        HANDLE hi = GetStdHandle(STD_INPUT_HANDLE); DWORD r = 0;
        say("원본 ISO 파일을 이 창에 끌어다 놓고 Enter 키를 누르세요.\n> ");
        if (!ReadConsoleW(hi, path, 1023, &r, NULL)) fail("입력을 읽을 수 없습니다.");
        path[r] = 0;
    }
    strip_path(path);
    if (!path[0]) fail("ISO 파일을 지정해 주세요.");
    run(path);
    {
        wchar_t outp[1100]; wchar_t *dot, *sl; char u8[3300];
        wcscpy(outp, path); dot = wcsrchr(outp, L'.'); sl = wcsrchr(outp, L'\\');
        if (dot && (!sl || dot > sl)) *dot = 0;
        wcscat(outp, L" [KOR].iso");
        WideCharToMultiByte(CP_UTF8, 0, outp, -1, u8, sizeof u8, NULL, NULL);
        say("\n완료했습니다.\n새 ISO: %s\n", u8);
    }
    wait_exit();
    return 0;
}
#else
int main(int argc, char **argv) {
    banner();
    if (argc < 2) { say("사용법: %s <원본 ISO>\n", argv[0]); return 2; }
    g_interactive = 0;
    run(argv[1]);
    say("\n완료했습니다.\n");
    return 0;
}
#endif
