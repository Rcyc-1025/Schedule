/* text_extract.c — 文件文本提取：
 *   1) 纯文本（.txt/.md/.csv）：UTF-8 BOM / UTF-16 LE/BE / GBK -> UTF-8；
 *   2) OOXML（.xlsx/.docx）：自实现 ZIP 迷你读取器（EOCD -> central directory
 *      -> local header，支持 method 0 stored 与 method 8 deflate），
 *      deflate 为 puff 风格紧凑 inflate（仅静态表，无 zlib 依赖），
 *      再从内嵌 XML 抽取单元格/段落文本。
 * 全部解析带长度边界检查，不假设输入 NUL 结尾；解压/转换结果补 \0。 */
#include "text_extract.h"
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define TEX_MAX_FILE   (20u * 1024u * 1024u)   /* 输入文件上限 20MB */
#define TEX_MAX_DECOMP (64u * 1024u * 1024u)   /* 解压输出上限 64MB */

static void set_err(wchar_t *err, int cap, const wchar_t *msg)
{
    if (!err || cap <= 0) return;
    wcsncpy(err, msg, (size_t)cap - 1);
    err[cap - 1] = 0;
}

/* ================= 动态 UTF-8 缓冲 ================= */

typedef struct { char *p; size_t len, cap; } SBuf;

static int sb_reserve(SBuf *b, size_t extra)
{
    size_t need = b->len + extra + 1;
    if (need <= b->cap) return 1;
    if (need > TEX_MAX_DECOMP) return 0;
    size_t nc = b->cap ? b->cap : 256;
    while (nc < need) nc <<= 1;
    char *np = (char *)realloc(b->p, nc);
    if (!np) return 0;
    b->p = np;
    b->cap = nc;
    return 1;
}

static int sb_put(SBuf *b, const char *s, size_t n)
{
    if (!sb_reserve(b, n)) return 0;
    if (n) { memcpy(b->p + b->len, s, n); b->len += n; }
    b->p[b->len] = 0;
    return 1;
}

static int sb_ch(SBuf *b, char c) { return sb_put(b, &c, 1); }

static int sb_put_cp(SBuf *b, unsigned cp)   /* Unicode 码点 -> UTF-8 */
{
    char u[4];
    int n = 0;
    if (cp < 0x80) {
        u[n++] = (char)cp;
    } else if (cp < 0x800) {
        u[n++] = (char)(0xC0 | (cp >> 6));
        u[n++] = (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        u[n++] = (char)(0xE0 | (cp >> 12));
        u[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        u[n++] = (char)(0x80 | (cp & 0x3F));
    } else {
        u[n++] = (char)(0xF0 | (cp >> 18));
        u[n++] = (char)(0x80 | ((cp >> 12) & 0x3F));
        u[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        u[n++] = (char)(0x80 | (cp & 0x3F));
    }
    return sb_put(b, u, (size_t)n);
}

/* ================= XML 文本反转义 ================= */

/* 将 [s,e) 内容反转义后追加到 b：&amp; &lt; &gt; &quot; &apos; &#ddd; &#xhh; */
static int xml_text_append(SBuf *b, const char *s, const char *e)
{
    while (s < e) {
        if (*s != '&') { if (!sb_ch(b, *s++)) return 0; continue; }
        const char *semi = memchr(s, ';', (size_t)(e - s));
        if (!semi || semi - s > 10) { if (!sb_ch(b, *s++)) return 0; continue; }
        size_t el = (size_t)(semi - s);           /* '&' 到 ';' 的距离（不含 ';'） */
        if (el == 4 && memcmp(s, "&amp;", 5) == 0) { if (!sb_ch(b, '&')) return 0; s = semi + 1; continue; }
        if (el == 3 && memcmp(s, "&lt;", 4) == 0)  { if (!sb_ch(b, '<')) return 0; s = semi + 1; continue; }
        if (el == 3 && memcmp(s, "&gt;", 4) == 0)  { if (!sb_ch(b, '>')) return 0; s = semi + 1; continue; }
        if (el == 5 && memcmp(s, "&quot;", 6) == 0)  { if (!sb_ch(b, '"')) return 0; s = semi + 1; continue; }
        if (el == 5 && memcmp(s, "&apos;", 6) == 0)  { if (!sb_ch(b, '\'')) return 0; s = semi + 1; continue; }
        if (el >= 3 && s[1] == '#') {
            int hex = (s[2] == 'x' || s[2] == 'X');
            const char *q = s + 2 + (hex ? 1 : 0);
            unsigned cp = 0;
            int ok = 1;
            while (q < semi) {
                int d;
                if (*q >= '0' && *q <= '9') d = *q - '0';
                else if (hex && *q >= 'a' && *q <= 'f') d = *q - 'a' + 10;
                else if (hex && *q >= 'A' && *q <= 'F') d = *q - 'A' + 10;
                else { ok = 0; break; }
                cp = cp * (hex ? 16u : 10u) + (unsigned)d;
                if (cp > 0x10FFFF) { ok = 0; break; }
                q++;
            }
            if (ok && q == semi && cp != 0) {
                if (!sb_put_cp(b, cp)) return 0;
                s = semi + 1;
                continue;
            }
        }
        /* 未知实体：原样输出 '&' 继续扫描 */
        if (!sb_ch(b, *s++)) return 0;
    }
    return 1;
}

/* ================= 朴素内存查找 ================= */

static const char *mem_find(const char *hay, size_t hn, const char *nd, size_t nn)
{
    if (nn == 0) return hay;
    if (hn < nn) return NULL;
    for (size_t i = 0; i + nn <= hn; i++)
        if (hay[i] == nd[0] && memcmp(hay + i, nd, nn) == 0) return hay + i;
    return NULL;
}

/* ================= 紧凑 puff 风格 inflate ================= */

#define MAXBITS 15

typedef struct {
    const unsigned char *in;  size_t inlen, inpos;
    int bitbuf, bitcnt;
    unsigned char *out; size_t outcap, outpos;
} Inf;

static int inf_bits(Inf *s, int need)
{
    long val = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->inpos >= s->inlen) return -1;
        val |= (long)s->in[s->inpos++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = (int)(val >> need);
    s->bitcnt -= need;
    return (int)(val & ((1L << need) - 1));
}

static int inf_stored(Inf *s)
{
    s->bitbuf = 0;
    s->bitcnt = 0;
    if (s->inlen - s->inpos < 4) return -2;
    unsigned len  = (unsigned)s->in[s->inpos]     | ((unsigned)s->in[s->inpos + 1] << 8);
    unsigned nlen = (unsigned)s->in[s->inpos + 2] | ((unsigned)s->in[s->inpos + 3] << 8);
    s->inpos += 4;
    if ((len ^ 0xFFFFu) != nlen) return -3;
    if (s->inlen - s->inpos < len) return -4;
    if (s->outcap - s->outpos < len) return -5;
    memcpy(s->out + s->outpos, s->in + s->inpos, len);
    s->inpos += len;
    s->outpos += len;
    return 0;
}

/* Huffman 码表：count[len]=该长度码字数，symbol[]=按长度分组的符号 */
typedef struct { short count[MAXBITS + 1]; short symbol[288]; } Huff;

static int huff_build(Huff *h, const short *length, int n)
{
    short offs[MAXBITS + 1];
    int symbol, len, left;
    for (len = 0; len <= MAXBITS; len++) h->count[len] = 0;
    for (symbol = 0; symbol < n; symbol++) h->count[length[symbol]]++;
    if (h->count[0] == n) return 0;               /* 无任何码字 */
    left = 1;
    for (len = 1; len <= MAXBITS; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return left;                /* 过载：非法表 */
    }
    offs[1] = 0;
    for (len = 1; len < MAXBITS; len++)
        offs[len + 1] = (short)(offs[len] + h->count[len]);
    for (symbol = 0; symbol < n; symbol++)
        if (length[symbol]) h->symbol[offs[length[symbol]]++] = (short)symbol;
    return left;                                  /* 0=完全, >0=未用完(允许) */
}

static int huff_decode(Inf *s, const Huff *h)
{
    int len = 1, code = 0, first = 0, index = 0;
    while (len <= MAXBITS) {
        int b = inf_bits(s, 1);
        if (b < 0) return b;
        code |= b;
        int count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first = (first + count) << 1;
        code <<= 1;
        len++;
    }
    return -9;                                    /* 码字超长：数据损坏 */
}

static const short LEN_BASE[29] = {
    3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
static const short LEN_EXTRA[29] = {
    0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
static const short DIST_BASE[30] = {
    1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,
    1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
static const short DIST_EXTRA[30] = {
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };

static int inf_codes(Inf *s, const Huff *lc, const Huff *dc)
{
    for (;;) {
        int sym = huff_decode(s, lc);
        if (sym < 0) return sym;
        if (sym < 256) {                          /* 字面字节 */
            if (s->outpos >= s->outcap) return -6;
            s->out[s->outpos++] = (unsigned char)sym;
        } else if (sym == 256) {                  /* 块结束 */
            return 0;
        } else {                                  /* 长度+距离匹配 */
            sym -= 257;
            if (sym >= 29) return -10;
            int eb = inf_bits(s, LEN_EXTRA[sym]);
            if (eb < 0) return eb;
            int len = LEN_BASE[sym] + eb;
            int dsym = huff_decode(s, dc);
            if (dsym < 0) return dsym;
            if (dsym >= 30) return -11;
            int ed = inf_bits(s, DIST_EXTRA[dsym]);
            if (ed < 0) return ed;
            size_t dist = (size_t)DIST_BASE[dsym] + (size_t)ed;
            if (dist > s->outpos) return -12;     /* 距离超过已输出量 */
            if (s->outcap - s->outpos < (size_t)len) return -13;
            while (len--) {
                s->out[s->outpos] = s->out[s->outpos - dist];
                s->outpos++;
            }
        }
    }
}

static int inf_fixed(Inf *s)
{
    static int init = 0;
    static Huff lc, dc;
    if (!init) {
        short lengths[288];
        int i;
        for (i = 0; i < 144; i++) lengths[i] = 8;
        for (; i < 256; i++) lengths[i] = 9;
        for (; i < 280; i++) lengths[i] = 7;
        for (; i < 288; i++) lengths[i] = 8;
        huff_build(&lc, lengths, 288);
        for (i = 0; i < 30; i++) lengths[i] = 5;
        huff_build(&dc, lengths, 30);
        init = 1;
    }
    return inf_codes(s, &lc, &dc);
}

static int inf_dynamic(Inf *s)
{
    static const short ORDER[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
    Huff lc, dc;
    short lengths[288 + 32];                      /* nlen<=286 + ndist<=30 */
    int a = inf_bits(s, 5), b5 = inf_bits(s, 5), c = inf_bits(s, 4);
    if (a < 0 || b5 < 0 || c < 0) return -1;
    int nlen = a + 257, ndist = b5 + 1, ncode = c + 4;
    if (nlen > 286 || ndist > 30) return -14;
    int index;
    for (index = 0; index < ncode; index++) {
        int v = inf_bits(s, 3);
        if (v < 0) return v;
        lengths[ORDER[index]] = (short)v;
    }
    for (; index < 19; index++) lengths[ORDER[index]] = 0;
    if (huff_build(&lc, lengths, 19) != 0) return -15;
    index = 0;
    while (index < nlen + ndist) {
        int sym = huff_decode(s, &lc);
        if (sym < 0) return sym;
        if (sym < 16) {
            lengths[index++] = (short)sym;
        } else {
            int len = 0, rep, v;
            if (sym == 16) {                      /* 复制前一长度 */
                if (index == 0) return -16;
                len = lengths[index - 1];
                v = inf_bits(s, 2); if (v < 0) return v;
                rep = 3 + v;
            } else if (sym == 17) {
                v = inf_bits(s, 3); if (v < 0) return v;
                rep = 3 + v;
            } else {
                v = inf_bits(s, 7); if (v < 0) return v;
                rep = 11 + v;
            }
            if (index + rep > nlen + ndist) return -17;
            while (rep--) lengths[index++] = (short)len;
        }
    }
    int err = huff_build(&lc, lengths, nlen);
    if (err < 0 || (err > 0 && nlen != lc.count[0] + lc.count[1])) return -18;
    err = huff_build(&dc, lengths + nlen, ndist);
    if (err < 0 || (err > 0 && ndist != dc.count[0] + dc.count[1])) return -19;
    return inf_codes(s, &lc, &dc);
}

/* 原始 deflate 流解压。成功返回 0 并写出 produced。 */
static int inflate_raw(const unsigned char *in, size_t inlen,
                       unsigned char *out, size_t outcap, size_t *produced)
{
    Inf s;
    s.in = in;  s.inlen = inlen;  s.inpos = 0;
    s.bitbuf = 0; s.bitcnt = 0;
    s.out = out; s.outcap = outcap; s.outpos = 0;
    int last = 0, err = 0;
    do {
        last = inf_bits(&s, 1);
        if (last < 0) { err = last; break; }
        int type = inf_bits(&s, 2);
        if (type < 0) { err = type; break; }
        if (type == 0)      err = inf_stored(&s);
        else if (type == 1) err = inf_fixed(&s);
        else if (type == 2) err = inf_dynamic(&s);
        else                err = -20;
        if (err) break;
    } while (!last);
    *produced = s.outpos;
    return err;
}

/* ================= ZIP 迷你读取器 ================= */

static unsigned rd16(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static unsigned rd32(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8) |
           ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

typedef struct {
    unsigned method, csize, usize;
    size_t   doff;      /* 成员数据起始偏移（local header 之后） */
} ZipEntry;

/* 在缓冲末尾（含 65535 字节注释区）查找 End of Central Directory */
static size_t zip_find_eocd(const unsigned char *b, size_t n)
{
    if (n < 22) return (size_t)-1;
    size_t lo = (n >= 22 + 65535) ? n - 22 - 65535 : 0;
    for (size_t i = n - 22 + 1; i-- > lo; )
        if (rd32(b + i) == 0x06054b50u) return i;
    return (size_t)-1;
}

/* 遍历 central directory 项，解析出 method/size/数据偏移。找到返回 1。 */
static int zip_entry_fill(const unsigned char *b, size_t n, size_t p, ZipEntry *e)
{
    unsigned lho = rd32(b + p + 42);
    if (lho + 30 > n || rd32(b + lho) != 0x04034b50u) return 0;
    unsigned lfn = rd16(b + lho + 26);
    unsigned lex = rd16(b + lho + 28);
    size_t doff = (size_t)lho + 30 + lfn + lex;
    if (doff > n) return 0;
    e->method = rd16(b + p + 10);
    e->csize  = rd32(b + p + 20);
    e->usize  = rd32(b + p + 24);
    if (e->csize > n - doff) return 0;
    e->doff = doff;
    return 1;
}

/* 遍历 central directory 查找指定名字的成员（忽略大小写）。找到返回 1。 */
static int zip_find_entry(const unsigned char *b, size_t n,
                          const char *name, ZipEntry *e)
{
    size_t eocd = zip_find_eocd(b, n);
    if (eocd == (size_t)-1) return 0;
    unsigned total  = rd16(b + eocd + 10);
    unsigned cdsize = rd32(b + eocd + 12);
    size_t   cdoff  = rd32(b + eocd + 16);
    if (cdoff > n || cdsize > n - cdoff) return 0;
    size_t p = cdoff, cdend = cdoff + cdsize;
    size_t nl = strlen(name);
    for (unsigned k = 0; k < total && p + 46 <= cdend; k++) {
        if (rd32(b + p) != 0x02014b50u) break;
        unsigned nlen = rd16(b + p + 28);
        unsigned elen = rd16(b + p + 30);
        unsigned clen = rd16(b + p + 32);
        const char *nm = (const char *)b + p + 46;
        size_t next = p + 46 + nlen + elen + clen;
        if (next > cdend) break;
        if (nlen == nl && _strnicmp(nm, name, nl) == 0)
            return zip_entry_fill(b, n, p, e);
        p = next;
    }
    return 0;
}

/* 查找第一个名字以 prefix 开头且以 suffix 结尾的成员（用于 sheet*.xml 回退） */
static int zip_find_entry_prefix(const unsigned char *b, size_t n,
                                 const char *prefix, const char *suffix,
                                 ZipEntry *e, char *nameOut, int nameCap)
{
    size_t eocd = zip_find_eocd(b, n);
    if (eocd == (size_t)-1) return 0;
    unsigned total  = rd16(b + eocd + 10);
    unsigned cdsize = rd32(b + eocd + 12);
    size_t   cdoff  = rd32(b + eocd + 16);
    if (cdoff > n || cdsize > n - cdoff) return 0;
    size_t p = cdoff, cdend = cdoff + cdsize;
    size_t plen = strlen(prefix), slen = strlen(suffix);
    for (unsigned k = 0; k < total && p + 46 <= cdend; k++) {
        if (rd32(b + p) != 0x02014b50u) break;
        unsigned nlen = rd16(b + p + 28);
        unsigned elen = rd16(b + p + 30);
        unsigned clen = rd16(b + p + 32);
        const char *nm = (const char *)b + p + 46;
        size_t next = p + 46 + nlen + elen + clen;
        if (next > cdend) break;
        if (nlen > plen + slen &&
            memcmp(nm, prefix, plen) == 0 &&
            memcmp(nm + nlen - slen, suffix, slen) == 0) {
            if (nameOut && nameCap > 0) {
                size_t cp = (size_t)nlen < (size_t)nameCap - 1
                          ? (size_t)nlen : (size_t)nameCap - 1;
                memcpy(nameOut, nm, cp);
                nameOut[cp] = 0;
            }
            return zip_entry_fill(b, n, p, e);
        }
        p = next;
    }
    return 0;
}

/* 解出指定成员到 out（内容补 \0，out->len 不含 \0）。成功返回 1。 */
static int zip_read_member(const unsigned char *b, size_t n,
                           const char *name, SBuf *out)
{
    ZipEntry e;
    if (!zip_find_entry(b, n, name, &e)) return 0;
    if (e.usize > TEX_MAX_DECOMP) return 0;
    if (e.method == 0) {                          /* stored */
        if (e.csize != e.usize) return 0;
        return sb_put(out, (const char *)b + e.doff, e.usize);
    }
    if (e.method != 8) return 0;                  /* 仅支持 deflate */
    unsigned char *dst = (unsigned char *)malloc((size_t)e.usize + 1);
    if (!dst) return 0;
    size_t produced = 0;
    int err = inflate_raw(b + e.doff, e.csize, dst, e.usize, &produced);
    if (err) { free(dst); return 0; }
    dst[produced] = 0;
    out->p = (char *)dst;                         /* 接管缓冲 */
    out->len = produced;
    out->cap = (size_t)e.usize + 1;
    return 1;
}

/* ================= xlsx 提取 ================= */

typedef struct { char **items; size_t n, cap; } StrList;

static int strlist_push(StrList *l, char *heapStr)
{
    if (l->n == l->cap) {
        size_t nc = l->cap ? l->cap * 2 : 16;
        char **ni = (char **)realloc(l->items, nc * sizeof(char *));
        if (!ni) return 0;
        l->items = ni;
        l->cap = nc;
    }
    l->items[l->n++] = heapStr;
    return 1;
}

static void strlist_free(StrList *l)
{
    for (size_t i = 0; i < l->n; i++) free(l->items[i]);
    free(l->items);
    l->items = NULL;
    l->n = l->cap = 0;
}

/* sharedStrings.xml：每个 <si> 内所有 <t>（含富文本 run）拼接为一个共享字符串 */
static int parse_shared_strings(const char *xml, size_t n, StrList *list)
{
    const char *end = xml + n;
    const char *p = xml;
    while (p < end) {
        const char *si = mem_find(p, (size_t)(end - p), "<si", 3);
        if (!si) break;
        const char *sgt = mem_find(si, (size_t)(end - si), ">", 1);
        if (!sgt) break;
        const char *send = mem_find(sgt, (size_t)(end - sgt), "</si>", 5);
        if (!send) break;
        SBuf sb;
        memset(&sb, 0, sizeof(sb));
        const char *q = sgt + 1;
        while (q < send) {
            const char *t = mem_find(q, (size_t)(send - q), "<t", 2);
            if (!t) break;
            const char *tgt = mem_find(t, (size_t)(send - t), ">", 1);
            if (!tgt) break;
            const char *tend = mem_find(tgt, (size_t)(send - tgt), "</t>", 4);
            if (!tend) break;
            if (!xml_text_append(&sb, tgt + 1, tend)) { free(sb.p); return 0; }
            q = tend + 4;
        }
        char *item = (char *)malloc(sb.len + 1);
        if (item) {
            if (sb.len) memcpy(item, sb.p, sb.len);
            item[sb.len] = 0;
            if (!strlist_push(list, item)) free(item);
        }
        free(sb.p);
        p = send + 5;
    }
    return 1;
}

/* 文本若含逗号/引号/换行则按 CSV 规则加引号转义后追加 */
static int sb_put_csv(SBuf *b, const char *s, size_t n)
{
    int needQ = 0;
    for (size_t i = 0; i < n; i++)
        if (s[i] == ',' || s[i] == '"' || s[i] == '\n' || s[i] == '\r') { needQ = 1; break; }
    if (!needQ) return sb_put(b, s, n);
    if (!sb_ch(b, '"')) return 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '"' && !sb_ch(b, '"')) return 0;
        if (!sb_ch(b, s[i])) return 0;
    }
    return sb_ch(b, '"');
}

static long parse_long_bounded(const char *s, const char *e)
{
    long v = 0;
    int any = 0;
    while (s < e && *s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        if (v > 10000000) return -1;
        any = 1;
        s++;
    }
    return any ? v : -1;
}

/* 在单元格内容 [body, bodyEnd) 中取 <v>...</v> 或 <t>...</t> 文本（反转义） */
static int cell_value_text(const char *body, const char *bodyEnd,
                           const char *tag, SBuf *tmp)
{
    const char *t = mem_find(body, (size_t)(bodyEnd - body), tag, 2);
    if (!t) return 1;                             /* 无值：空单元格 */
    const char *tgt = mem_find(t, (size_t)(bodyEnd - t), ">", 1);
    if (!tgt) return 1;
    const char *closeTag = (tag[1] == 'v') ? "</v>" : "</t>";
    const char *tend = mem_find(tgt, (size_t)(bodyEnd - tgt), closeTag, 4);
    if (!tend) return 1;
    return xml_text_append(tmp, tgt + 1, tend);
}

/* sheet XML：逐 <row> / 逐 <c>，单元格以 ", " 连接，行间 \n；空行跳过 */
static int parse_sheet_xml(const char *xml, size_t n,
                           const StrList *shared, SBuf *out)
{
    const char *end = xml + n;
    const char *p = xml;
    int rowsOut = 0;
    while (p < end) {
        const char *row = mem_find(p, (size_t)(end - p), "<row", 4);
        if (!row) break;
        const char *rend = mem_find(row, (size_t)(end - row), "</row>", 6);
        const char *rowEnd = rend ? rend : end;
        SBuf line;
        memset(&line, 0, sizeof(line));
        int cells = 0;
        const char *q = row;
        while (q < rowEnd) {
            const char *c = mem_find(q, (size_t)(rowEnd - q), "<c", 2);
            if (!c) break;
            const char *cgt = mem_find(c, (size_t)(rowEnd - c), ">", 1);
            if (!cgt) break;
            int selfClose = (cgt[-1] == '/');
            const char *body, *bodyEnd, *cellNext;
            if (selfClose) {
                body = bodyEnd = cgt;             /* 自闭合：无内容 */
                cellNext = cgt + 1;
            } else {
                body = cgt + 1;
                const char *cend = mem_find(body, (size_t)(rowEnd - body), "</c>", 4);
                bodyEnd = cend ? cend : rowEnd;
                cellNext = cend ? cend + 4 : rowEnd;
            }
            /* 单元格类型：tag 区间 [c, cgt) 中查找 t="s" / t="inlineStr" */
            const char *tagS = mem_find(c, (size_t)(cgt - c), "t=\"s\"", 5);
            const char *tagI = mem_find(c, (size_t)(cgt - c), "t=\"inlineStr\"", 13);
            const char *txt = NULL;
            size_t txtLen = 0;
            SBuf tmp;
            memset(&tmp, 0, sizeof(tmp));
            int ok = 1;
            if (tagS) {
                const char *v = mem_find(body, (size_t)(bodyEnd - body), "<v", 2);
                if (v) {
                    const char *vgt = mem_find(v, (size_t)(bodyEnd - v), ">", 1);
                    const char *vend = vgt
                        ? mem_find(vgt, (size_t)(bodyEnd - vgt), "</v>", 4) : NULL;
                    if (vgt && vend) {
                        long idx = parse_long_bounded(vgt + 1, vend);
                        if (idx >= 0 && shared && (size_t)idx < shared->n && shared->items[idx]) {
                            txt = shared->items[idx];
                            txtLen = strlen(txt);
                        }
                    }
                }
            } else if (tagI) {
                ok = cell_value_text(body, bodyEnd, "<t", &tmp);
                txt = tmp.p;
                txtLen = tmp.len;
            } else {
                ok = cell_value_text(body, bodyEnd, "<v", &tmp);
                txt = tmp.p;
                txtLen = tmp.len;
            }
            if (ok && txt && txtLen) {
                if (cells > 0 && !sb_put(&line, ", ", 2)) ok = 0;
                if (ok && !sb_put_csv(&line, txt, txtLen)) ok = 0;
                if (ok) cells++;
            }
            free(tmp.p);
            if (!ok) { free(line.p); return 0; }
            q = cellNext;
        }
        if (cells > 0 && line.len) {
            if (rowsOut > 0 && !sb_ch(out, '\n')) { free(line.p); return 0; }
            if (!sb_put(out, line.p, line.len)) { free(line.p); return 0; }
            rowsOut++;
        }
        free(line.p);
        if (!rend) break;                         /* 无 </row>：已到末尾 */
        p = rowEnd;
    }
    return 1;
}

/* ================= docx 提取 ================= */

/* document.xml：按顺序提取 <w:t> 文本；</w:p> 换行；<w:tab/> 输出 \t；
 * 其余标签跳过（仅输出文本节点，不输出标签间空白）。 */
static int parse_docx_xml(const char *xml, size_t n, SBuf *out)
{
    const char *end = xml + n;
    const char *p = xml;
    while (p < end) {
        const char *lt = memchr(p, '<', (size_t)(end - p));
        if (!lt) break;
        if ((size_t)(end - lt) < 3) break;
        const char *tag = lt + 1;
        size_t tagRoom = (size_t)(end - tag);
        if (tagRoom >= 5 && memcmp(tag, "/w:p>", 5) == 0) {
            if (!sb_ch(out, '\n')) return 0;
            p = tag + 5;
            continue;
        }
        if (tagRoom >= 5 && memcmp(tag, "w:tab", 5) == 0) {
            const char *gt = memchr(tag, '>', tagRoom);
            if (gt && gt[-1] == '/') {
                if (!sb_ch(out, '\t')) return 0;
                p = gt + 1;
                continue;
            }
        }
        if (tagRoom >= 4 && memcmp(tag, "w:t", 3) == 0 &&
            (tag[3] == '>' || tag[3] == ' ')) {
            const char *gt = memchr(tag, '>', tagRoom);
            if (!gt) break;
            if (gt[-1] != '/') {                  /* 非自闭合：取文本 */
                if ((size_t)(end - gt) < 6) break;
                const char *tclose = mem_find(gt + 1, (size_t)(end - gt - 1), "</w:t>", 6);
                if (!tclose) break;
                if (!xml_text_append(out, gt + 1, tclose)) return 0;
                p = tclose + 6;
                continue;
            }
            p = gt + 1;                           /* <w:t/>：空文本 */
            continue;
        }
        /* 其他标签：整体跳过 */
        const char *gt2 = memchr(tag, '>', tagRoom);
        if (!gt2) break;
        p = gt2 + 1;
    }
    return 1;
}

/* ================= 纯文本路径（BOM/UTF-16/GBK） ================= */

/* 在字节流上判断是否为合法 UTF-8（含纯 ASCII）；截断/非法前导即判非。 */
static int is_valid_utf8(const unsigned char *p, size_t n)
{
    size_t i = 0;
    while (i < n) {
        unsigned char c = p[i];
        if (c < 0x80) { i++; continue; }
        int need;
        if      ((c & 0xE0) == 0xC0) need = 1;
        else if ((c & 0xF0) == 0xE0) need = 2;
        else if ((c & 0xF8) == 0xF0) need = 3;
        else return 0;
        if (i + need >= n) return 0;
        for (int k = 1; k <= need; k++)
            if ((p[i + k] & 0xC0) != 0x80) return 0;
        i += need + 1;
    }
    return 1;
}

/* UTF-16 (wchar_t 数组) -> UTF-8 heap（含 \0，outLen 不含）。失败返回 0。 */
static int w16_to_u8_alloc(const wchar_t *ws, int cnt, char **out, size_t *outLen)
{
    int u8n = WideCharToMultiByte(CP_UTF8, 0, ws, cnt, NULL, 0, NULL, NULL);
    if (u8n <= 0) return 0;
    char *u8 = (char *)malloc((size_t)u8n + 1);
    if (!u8) return 0;
    WideCharToMultiByte(CP_UTF8, 0, ws, cnt, u8, u8n, NULL, NULL);
    u8[u8n] = 0;
    *out = u8;
    *outLen = (size_t)u8n;
    return 1;
}

/* 原始字节 -> UTF-8 heap。成功返回 1。 */
static int decode_text(const unsigned char *p, size_t n,
                       char **out, size_t *outLen)
{
    /* UTF-16 LE BOM */
    if (n >= 2 && p[0] == 0xFF && p[1] == 0xFE) {
        int cnt = (int)((n - 2) / sizeof(wchar_t));
        if (cnt > 0) return w16_to_u8_alloc((const wchar_t *)(p + 2), cnt, out, outLen);
        return 0;
    }
    /* UTF-16 BE BOM：交换字节后按 LE 转换 */
    if (n >= 2 && p[0] == 0xFE && p[1] == 0xFF) {
        size_t cnt = (n - 2) / sizeof(wchar_t);
        int ok = 0;
        if (cnt > 0) {
            wchar_t *tmp = (wchar_t *)malloc(cnt * sizeof(wchar_t));
            if (tmp) {
                for (size_t i = 0; i < cnt; i++)
                    tmp[i] = (wchar_t)(((unsigned)p[2 + i * 2] << 8) | p[3 + i * 2]);
                ok = w16_to_u8_alloc(tmp, (int)cnt, out, outLen);
                free(tmp);
            }
        }
        return ok;
    }
    /* UTF-8 BOM */
    if (n >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) { p += 3; n -= 3; }
    if (is_valid_utf8(p, n)) {
        char *u8 = (char *)malloc(n + 1);
        if (!u8) return 0;
        memcpy(u8, p, n);
        u8[n] = 0;
        *out = u8;
        *outLen = n;
        return 1;
    }
    /* 视作 GBK（CP_ACP，简体中文系统通常 936） */
    int cnt = MultiByteToWideChar(CP_ACP, 0, (LPCSTR)p, (int)n, NULL, 0);
    if (cnt <= 0) return 0;
    wchar_t *w = (wchar_t *)malloc((size_t)(cnt + 1) * sizeof(wchar_t));
    if (!w) return 0;
    MultiByteToWideChar(CP_ACP, 0, (LPCSTR)p, (int)n, w, cnt);
    w[cnt] = 0;
    int ok = w16_to_u8_alloc(w, cnt, out, outLen);
    free(w);
    return ok;
}

/* ================= OOXML 分派与提取 ================= */

static int extract_xlsx(const unsigned char *b, size_t n,
                        char **out, size_t *outLen, wchar_t *err, int errCap)
{
    StrList shared;
    memset(&shared, 0, sizeof(shared));
    SBuf ss;
    memset(&ss, 0, sizeof(ss));
    if (zip_read_member(b, n, "xl/sharedStrings.xml", &ss)) {
        parse_shared_strings(ss.p ? ss.p : "", ss.len, &shared);
    }
    free(ss.p);

    /* 工作表：优先 sheet1.xml，否则第一份 xl/worksheets/sheet*.xml */
    SBuf sheet;
    memset(&sheet, 0, sizeof(sheet));
    int haveSheet = zip_read_member(b, n, "xl/worksheets/sheet1.xml", &sheet);
    if (!haveSheet) {
        char sheetName[128];
        ZipEntry e;
        if (zip_find_entry_prefix(b, n, "xl/worksheets/sheet", ".xml", &e,
                                  sheetName, (int)sizeof(sheetName)))
            haveSheet = zip_read_member(b, n, sheetName, &sheet);
    }
    if (!haveSheet) {
        strlist_free(&shared);
        set_err(err, errCap, L"xlsx 中未找到工作表（文件可能损坏）");
        return 0;
    }

    SBuf txt;
    memset(&txt, 0, sizeof(txt));
    int ok = parse_sheet_xml(sheet.p ? sheet.p : "", sheet.len, &shared, &txt);
    free(sheet.p);
    strlist_free(&shared);
    if (!ok || !txt.p || txt.len == 0) {
        free(txt.p);
        set_err(err, errCap, L"xlsx 工作表内容为空或解析失败");
        return 0;
    }
    *out = txt.p;
    *outLen = txt.len;
    return 1;
}

static int extract_docx(const unsigned char *b, size_t n,
                        char **out, size_t *outLen, wchar_t *err, int errCap)
{
    SBuf doc;
    memset(&doc, 0, sizeof(doc));
    if (!zip_read_member(b, n, "word/document.xml", &doc)) {
        set_err(err, errCap, L"docx 中未找到 word/document.xml（文件可能损坏）");
        return 0;
    }
    SBuf txt;
    memset(&txt, 0, sizeof(txt));
    int ok = parse_docx_xml(doc.p ? doc.p : "", doc.len, &txt);
    free(doc.p);
    if (!ok || !txt.p || txt.len == 0) {
        free(txt.p);
        set_err(err, errCap, L"docx 内容为空或解析失败");
        return 0;
    }
    *out = txt.p;
    *outLen = txt.len;
    return 1;
}

/* 按扩展名分派；无 .xlsx/.docx 扩展名时按 ZIP 内部成员嗅探 */
static int extract_ooxml(const unsigned char *b, size_t n, const wchar_t *path,
                         char **out, size_t *outLen, wchar_t *err, int errCap)
{
    const wchar_t *dot = wcsrchr(path, L'.');
    int isDocx = 0, isXlsx = 0;
    if (dot) {
        if (_wcsicmp(dot, L".docx") == 0) isDocx = 1;
        else if (_wcsicmp(dot, L".xlsx") == 0) isXlsx = 1;
    }
    if (!isDocx && !isXlsx) {
        ZipEntry probe;
        if (zip_find_entry(b, n, "word/document.xml", &probe)) isDocx = 1;
        else if (zip_find_entry(b, n, "xl/sharedStrings.xml", &probe)) isXlsx = 1;
        else if (zip_find_entry_prefix(b, n, "xl/worksheets/sheet", ".xml", &probe,
                                       NULL, 0)) isXlsx = 1;
        else {
            set_err(err, errCap, L"压缩包中未找到 xlsx/docx 内容");
            return 0;
        }
    }
    if (isDocx) return extract_docx(b, n, out, outLen, err, errCap);
    return extract_xlsx(b, n, out, outLen, err, errCap);
}

/* ================= 文件读取与总入口 ================= */

static unsigned char *read_file_all(const wchar_t *path, size_t *lenOut,
                                    wchar_t *err, int errCap)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        set_err(err, errCap, L"无法打开文件");
        return NULL;
    }
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0) {
        CloseHandle(h);
        set_err(err, errCap, L"文件为空或无法获取大小");
        return NULL;
    }
    if (sz.QuadPart > (LONGLONG)TEX_MAX_FILE) {
        CloseHandle(h);
        set_err(err, errCap, L"文件超过 20MB，请精简后再试");
        return NULL;
    }
    size_t n = (size_t)sz.QuadPart;
    unsigned char *buf = (unsigned char *)malloc(n + 1);
    if (!buf) {
        CloseHandle(h);
        set_err(err, errCap, L"内存不足");
        return NULL;
    }
    DWORD rd = 0;
    if (!ReadFile(h, buf, (DWORD)n, &rd, NULL) || rd != (DWORD)n) {
        free(buf);
        CloseHandle(h);
        set_err(err, errCap, L"读取文件失败");
        return NULL;
    }
    CloseHandle(h);
    buf[n] = 0;
    *lenOut = n;
    return buf;
}

int extract_text_from_file(const wchar_t *path, char **out, long *outLen,
                           wchar_t *err, int errCap)
{
    *out = NULL;
    if (outLen) *outLen = 0;
    if (err && errCap > 0) err[0] = 0;

    size_t rawLen = 0;
    unsigned char *raw = read_file_all(path, &rawLen, err, errCap);
    if (!raw) return 0;

    char *result = NULL;
    size_t resLen = 0;
    int ok;
    if (rawLen >= 4 && raw[0] == 0xD0 && raw[1] == 0xCF &&
        raw[2] == 0x11 && raw[3] == 0xE0) {
        /* OLE 复合文档（旧 .doc/.xls） */
        set_err(err, errCap, L"旧格式（.doc/.xls）请先另存为 .docx/.xlsx/CSV 再导入");
        ok = 0;
    } else if (rawLen >= 4 && raw[0] == 'P' && raw[1] == 'K' &&
               raw[2] == 0x03 && raw[3] == 0x04) {
        ok = extract_ooxml(raw, rawLen, path, &result, &resLen, err, errCap);
    } else {
        ok = decode_text(raw, rawLen, &result, &resLen);
        if (!ok) set_err(err, errCap, L"编码转换失败（请存为 UTF-8 文本）");
    }
    free(raw);
    if (!ok) return 0;
    if (!result || resLen == 0) {
        free(result);
        set_err(err, errCap, L"文件内容为空");
        return 0;
    }
    *out = result;
    if (outLen) *outLen = (long)resLen;
    return 1;
}
