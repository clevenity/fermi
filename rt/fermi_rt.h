#ifndef FERMI_RT_H
#define FERMI_RT_H
#include <stdint.h>

typedef struct { int64_t *data; int64_t len; int64_t cap; } FeArray;

#define FE_MAP_BUCKETS 64
typedef struct FeMapEntry { const char *key; int64_t val; struct FeMapEntry *next; } FeMapEntry;
typedef struct { FeMapEntry *buckets[FE_MAP_BUCKETS]; } FeMap;

void    fe_print_bool(int v);
void    fe_print_i32(int32_t v);
void    fe_print_i64(int64_t v);
void    fe_print_f32(float v);
void    fe_print_f64(double v);
void    fe_print_char(char v);
void    fe_print_str(const char *s);
void    fe_println_bool(int v);
void    fe_println_i32(int32_t v);
void    fe_println_i64(int64_t v);
void    fe_println_f32(float v);
void    fe_println_f64(double v);
void    fe_println_char(char v);
void    fe_println_str(const char *s);
void    fe_println_void(void);
void    fe_flush(void);
char   *fe_input(void);

int64_t fe_time(void);
void    fe_sleep(int64_t ms);
void    fe_exit(int32_t code);

int     fe_len(const char *s);
char   *fe_concat(const char *a, const char *b);
char   *fe_to_upper(const char *s);
char   *fe_to_lower(const char *s);
char   *fe_trim(const char *s);
int     fe_contains(const char *s, const char *sub);
int     fe_starts_with(const char *s, const char *prefix);
int     fe_ends_with(const char *s, const char *suffix);
char   *fe_replace(const char *s, const char *from, const char *to);
int     fe_index_of(const char *s, const char *sub);
char   *fe_slice(const char *s, int start, int end);
int     fe_parse_int(const char *s);
float   fe_parse_float(const char *s);
char   *fe_int_to_str(int32_t v);
char   *fe_float_to_str(float v);

int32_t fe_abs(int32_t v);
float   fe_absf(float v);
float   fe_sqrt(float v);
float   fe_powf(float b, float e);
float   fe_floor(float v);
float   fe_ceil(float v);
float   fe_round(float v);
int32_t fe_min(int32_t a, int32_t b);
int32_t fe_max(int32_t a, int32_t b);
float   fe_minf(float a, float b);
float   fe_maxf(float a, float b);
int32_t fe_clamp(int32_t v, int32_t lo, int32_t hi);
float   fe_sin(float v);
float   fe_cos(float v);
float   fe_tan(float v);
float   fe_log(float v);
float   fe_log2f(float v);
float   fe_log10f(float v);

void   *fe_alloc(int64_t sz);
void   *fe_realloc(void *p, int64_t sz);
void    fe_memcopy(void *dst, const void *src, int64_t n);
void    fe_memset(void *dst, int32_t val, int64_t n);
int32_t fe_memcmp(const void *a, const void *b, int64_t n);

char   *fe_getenv(const char *name);
int32_t fe_setenv(const char *name, const char *val);
char   *fe_getcwd(void);
int32_t fe_chdir(const char *path);
char   *fe_read_file(const char *path);
void    fe_write_file(const char *path, const char *content);

void    fe_region_push(void);
void   *fe_region_alloc(int64_t sz);
void    fe_region_pop(void);

FeArray *fe_array_new(int64_t cap);
void     fe_array_push(FeArray *arr, int64_t val);
int64_t  fe_array_pop(FeArray *arr);
int64_t  fe_array_get(FeArray *arr, int64_t idx);
void     fe_array_set(FeArray *arr, int64_t idx, int64_t val);
int64_t  fe_array_len(FeArray *arr);
void     fe_array_free(FeArray *arr);

FeMap  *fe_map_new(void);
void    fe_map_set(FeMap *map, const char *key, int64_t val);
int64_t fe_map_get(FeMap *map, const char *key);
int32_t fe_map_has(FeMap *map, const char *key);
void    fe_map_delete(FeMap *map, const char *key);
void    fe_map_free(FeMap *map);

void    __fermi_panic(const char *msg);

#endif
