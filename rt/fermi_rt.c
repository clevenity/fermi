#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include "fermi_rt.h"

static void write_all(int fd, const char *buf, int n) {
    while (n > 0) {
        int r = (int)write(fd, buf, (size_t)n);
        if (r <= 0) break;
        buf += r; n -= r;
    }
}

static int fmt_f64(char *buf, int cap, double v) {
    if (v != v) { memcpy(buf, "nan", 4); return 3; }
    if (v == v + 1.0) {
        if (v > 0) { memcpy(buf, "inf", 4); return 3; }
        else       { memcpy(buf, "-inf", 5); return 4; }
    }
    int n = snprintf(buf, (size_t)cap, "%g", v);
    return n < 0 ? 0 : (n >= cap ? cap - 1 : n);
}

void fe_print_f64(double v) {
    char buf[64];
    int n = fmt_f64(buf, sizeof(buf), v);
    write_all(1, buf, n);
}
void fe_print_f32(float v)  { fe_print_f64((double)v); }

void fe_println_f64(double v) {
    char buf[65];
    int n = fmt_f64(buf, 64, v);
    buf[n] = '\n';
    write_all(1, buf, n + 1);
}
void fe_println_f32(float v) { fe_println_f64((double)v); }

#if !(defined(__linux__) && defined(__x86_64__))

void fe_print_str(const char *s) {
    if (s) write_all(1, s, (int)strlen(s));
}

void fe_println_str(const char *s) {
    if (s) write_all(1, s, (int)strlen(s));
    write_all(1, "\n", 1);
}

void fe_println_void(void) {
    write_all(1, "\n", 1);
}

void fe_flush(void) {
}

void fe_print_bool(int v) {
    if (v) write_all(1, "true", 4);
    else write_all(1, "false", 5);
}

void fe_println_bool(int v) {
    if (v) write_all(1, "true\n", 5);
    else write_all(1, "false\n", 6);
}

void fe_print_char(char v) {
    write_all(1, &v, 1);
}

void fe_println_char(char v) {
    char buf[2] = {v, '\n'};
    write_all(1, buf, 2);
}

static int fmt_i64_internal(char *buf, int64_t v) {
    char tmp[32];
    int i = 0;
    uint64_t uv = (v < 0) ? (~(uint64_t)v + 1) : (uint64_t)v;
    
    if (v == 0) {
        tmp[i++] = '0';
    } else {
        while (uv > 0) {
            tmp[i++] = (char)('0' + (uv % 10));
            uv /= 10;
        }
    }
    int len = 0;
    if (v < 0) {
        buf[len++] = '-';
    }
    for (int j = i - 1; j >= 0; j--) {
        buf[len++] = tmp[j];
    }
    return len;
}

void fe_print_i64(int64_t v) {
    char buf[32];
    int len = fmt_i64_internal(buf, v);
    write_all(1, buf, len);
}

void fe_println_i64(int64_t v) {
    char buf[33];
    int len = fmt_i64_internal(buf, v);
    buf[len++] = '\n';
    write_all(1, buf, len);
}

void fe_print_i32(int32_t v) {
    fe_print_i64((int64_t)v);
}

void fe_println_i32(int32_t v) {
    fe_println_i64((int64_t)v);
}

char *fe_input(void) {
    static char fe_Libuf[4097];
    ssize_t r = read(0, fe_Libuf, 4096);
    if (r <= 0) {
        fe_Libuf[0] = '\0';
    } else {
        fe_Libuf[r] = '\0';
        if (fe_Libuf[r - 1] == '\n') {
            fe_Libuf[r - 1] = '\0';
        }
    }
    return fe_Libuf;
}

void fe_exit(int32_t code) {
    _exit(code);
}

#endif

int64_t fe_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + (int64_t)ts.tv_nsec / 1000000;
}

void fe_sleep(int64_t ms) {
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {}
}

int fe_len(const char *s) { return s ? (int)strlen(s) : 0; }

char *fe_concat(const char *a, const char *b) {
    if (!a) a = "";
    if (!b) b = "";
    size_t la = strlen(a), lb = strlen(b);
    char *r = malloc(la + lb + 1);
    memcpy(r, a, la); memcpy(r + la, b, lb); r[la + lb] = '\0';
    return r;
}

char *fe_to_upper(const char *s) {
    if (!s) return strdup("");
    char *r = strdup(s);
    for (char *p = r; *p; p++) *p = (char)toupper((unsigned char)*p);
    return r;
}

char *fe_to_lower(const char *s) {
    if (!s) return strdup("");
    char *r = strdup(s);
    for (char *p = r; *p; p++) *p = (char)tolower((unsigned char)*p);
    return r;
}

char *fe_trim(const char *s) {
    if (!s) return strdup("");
    while (isspace((unsigned char)*s)) s++;
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len-1])) len--;
    char *r = malloc(len + 1); memcpy(r, s, len); r[len] = '\0';
    return r;
}

int fe_contains(const char *s, const char *sub) { return s && sub && strstr(s, sub) != NULL; }

int fe_starts_with(const char *s, const char *p) {
    if (!s || !p) return 0;
    return strncmp(s, p, strlen(p)) == 0;
}

int fe_ends_with(const char *s, const char *suf) {
    if (!s || !suf) return 0;
    size_t ls = strlen(s), lf = strlen(suf);
    return ls >= lf && strcmp(s + ls - lf, suf) == 0;
}

char *fe_replace(const char *s, const char *from, const char *to) {
    if (!s || !from || !to) return strdup(s ? s : "");
    size_t lf = strlen(from), lt = strlen(to);
    if (!lf) return strdup(s);
    int count = 0;
    const char *p = s;
    while ((p = strstr(p, from))) { count++; p += lf; }
    size_t ls = strlen(s);
    char *r = malloc(ls + (size_t)count * (lt > lf ? lt - lf : 0) * 100 + 1);
    char *w = r; p = s;
    const char *f;
    while ((f = strstr(p, from))) {
        size_t before = (size_t)(f - p); memcpy(w, p, before); w += before;
        memcpy(w, to, lt); w += lt; p = f + lf;
    }
    strcpy(w, p); return r;
}

int fe_index_of(const char *s, const char *sub) {
    if (!s || !sub) return -1;
    const char *p = strstr(s, sub);
    return p ? (int)(p - s) : -1;
}

char *fe_slice(const char *s, int start, int end) {
    if (!s) return strdup("");
    int len = (int)strlen(s);
    if (start < 0) start = 0;
    if (end > len) end = len;
    if (start > end) start = end;
    int n = end - start;
    char *r = malloc((size_t)n + 1); memcpy(r, s + start, (size_t)n); r[n] = '\0';
    return r;
}

int   fe_parse_int(const char *s)   { return s ? atoi(s) : 0; }
float fe_parse_float(const char *s) { return s ? (float)atof(s) : 0.0f; }

char *fe_int_to_str(int32_t v) {
    char buf[32]; snprintf(buf, sizeof(buf), "%d", v); return strdup(buf);
}
char *fe_float_to_str(float v) {
    char buf[64]; snprintf(buf, sizeof(buf), "%g", (double)v); return strdup(buf);
}

int32_t fe_abs(int32_t v)          { return v < 0 ? -v : v; }
float   fe_absf(float v)           { return __builtin_fabsf(v); }
float   fe_sqrt(float v)           { return __builtin_sqrtf(v); }
float   fe_powf(float b, float e)  { return __builtin_powf(b, e); }
float   fe_floor(float v)          { return __builtin_floorf(v); }
float   fe_ceil(float v)           { return __builtin_ceilf(v); }
float   fe_round(float v)          { return __builtin_roundf(v); }
int32_t fe_min(int32_t a, int32_t b) { return a < b ? a : b; }
int32_t fe_max(int32_t a, int32_t b) { return a > b ? a : b; }
float   fe_minf(float a, float b)  { return a < b ? a : b; }
float   fe_maxf(float a, float b)  { return a > b ? a : b; }
int32_t fe_clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
float   fe_sin(float v)            { return __builtin_sinf(v); }
float   fe_cos(float v)            { return __builtin_cosf(v); }
float   fe_tan(float v)            { return __builtin_tanf(v); }
float   fe_log(float v)            { return __builtin_logf(v); }
float   fe_log2f(float v)          { return __builtin_log2f(v); }
float   fe_log10f(float v)         { return __builtin_log10f(v); }

void   *fe_alloc(int64_t sz)               { return malloc((size_t)sz); }
void   *fe_realloc(void *p, int64_t sz)    { return realloc(p, (size_t)sz); }
void    fe_memcopy(void *d, const void *s, int64_t n) { memmove(d, s, (size_t)n); }
void    fe_memset(void *d, int32_t v, int64_t n)      { memset(d, v, (size_t)n); }
int32_t fe_memcmp(const void *a, const void *b, int64_t n) { return memcmp(a, b, (size_t)n); }

char   *fe_getenv(const char *n)  { char *v = getenv(n); return v ? strdup(v) : strdup(""); }
int32_t fe_setenv(const char *n, const char *v) { return setenv(n, v, 1); }
char   *fe_getcwd(void)           { char buf[4096]; return strdup(getcwd(buf, sizeof(buf)) ? buf : ""); }
int32_t fe_chdir(const char *p)   { return chdir(p); }

char *fe_read_file(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return strdup("");
    off_t sz = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    char *buf = malloc((size_t)sz + 1);
    ssize_t r = read(fd, buf, (size_t)sz);
    buf[r > 0 ? r : 0] = '\0';
    close(fd);
    return buf;
}

void fe_write_file(const char *path, const char *content) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;
    if (content) (void)write(fd, content, strlen(content));
    close(fd);
}

typedef struct RegNode { void *ptr; struct RegNode *next; } RegNode;
#define FE_MAX_REGION_DEPTH 32
static RegNode *fe_region_heads[FE_MAX_REGION_DEPTH];
static int      fe_region_depth = 0;

void fe_region_push(void) {
    if (fe_region_depth < FE_MAX_REGION_DEPTH)
        fe_region_heads[fe_region_depth++] = NULL;
}
void *fe_region_alloc(int64_t sz) {
    void *p = malloc((size_t)sz);
    if (fe_region_depth > 0) {
        RegNode *node = malloc(sizeof(RegNode));
        node->ptr  = p;
        node->next = fe_region_heads[fe_region_depth - 1];
        fe_region_heads[fe_region_depth - 1] = node;
    }
    return p;
}
void fe_region_pop(void) {
    if (fe_region_depth <= 0) return;
    RegNode *n = fe_region_heads[--fe_region_depth];
    while (n) {
        free(n->ptr);
        RegNode *next = n->next;
        free(n);
        n = next;
    }
}

FeArray *fe_array_new(int64_t cap) {
    FeArray *a  = malloc(sizeof(FeArray));
    a->cap  = cap > 0 ? cap : 8;
    a->len  = 0;
    a->data = malloc((size_t)a->cap * sizeof(int64_t));
    return a;
}
void fe_array_push(FeArray *a, int64_t val) {
    if (!a) return;
    if (a->len >= a->cap) {
        a->cap *= 2;
        a->data = realloc(a->data, (size_t)a->cap * sizeof(int64_t));
    }
    a->data[a->len++] = val;
}
int64_t fe_array_pop(FeArray *a)             { return (!a || a->len <= 0) ? 0 : a->data[--a->len]; }
int64_t fe_array_get(FeArray *a, int64_t i)  { return (!a || i < 0 || i >= a->len) ? 0 : a->data[i]; }
void    fe_array_set(FeArray *a, int64_t i, int64_t v) { if (a && i >= 0 && i < a->len) a->data[i] = v; }
int64_t fe_array_len(FeArray *a)             { return a ? a->len : 0; }
void    fe_array_free(FeArray *a)            { if (a) { free(a->data); free(a); } }

static uint32_t fe_map_hash(const char *k) {
    uint32_t h = 2166136261u;
    for (; *k; k++) h = (h ^ (uint8_t)*k) * 16777619u;
    return h & (FE_MAP_BUCKETS - 1);
}

FeMap *fe_map_new(void) { return calloc(1, sizeof(FeMap)); }

void fe_map_set(FeMap *m, const char *key, int64_t val) {
    if (!m || !key) return;
    uint32_t h = fe_map_hash(key);
    for (FeMapEntry *e = m->buckets[h]; e; e = e->next)
        if (strcmp(e->key, key) == 0) { e->val = val; return; }
    FeMapEntry *e = malloc(sizeof(FeMapEntry));
    e->key = strdup(key); e->val = val; e->next = m->buckets[h];
    m->buckets[h] = e;
}

int64_t fe_map_get(FeMap *m, const char *key) {
    if (!m || !key) return 0;
    uint32_t h = fe_map_hash(key);
    for (FeMapEntry *e = m->buckets[h]; e; e = e->next)
        if (strcmp(e->key, key) == 0) return e->val;
    return 0;
}

int32_t fe_map_has(FeMap *m, const char *key) {
    if (!m || !key) return 0;
    uint32_t h = fe_map_hash(key);
    for (FeMapEntry *e = m->buckets[h]; e; e = e->next)
        if (strcmp(e->key, key) == 0) return 1;
    return 0;
}

void fe_map_delete(FeMap *m, const char *key) {
    if (!m || !key) return;
    uint32_t h = fe_map_hash(key);
    FeMapEntry **pp = &m->buckets[h];
    while (*pp) {
        if (strcmp((*pp)->key, key) == 0) {
            FeMapEntry *e = *pp; *pp = e->next;
            free((void *)e->key); free(e); return;
        }
        pp = &(*pp)->next;
    }
}

void fe_map_free(FeMap *m) {
    if (!m) return;
    for (int i = 0; i < FE_MAP_BUCKETS; i++) {
        FeMapEntry *e = m->buckets[i];
        while (e) {
            FeMapEntry *n = e->next;
            free((void *)e->key); free(e); e = n;
        }
    }
    free(m);
}

void __fermi_panic(const char *msg) {
    if (msg) (void)write(2, msg, strlen(msg));
    (void)write(2, "\n", 1);
    _exit(134); 
}
