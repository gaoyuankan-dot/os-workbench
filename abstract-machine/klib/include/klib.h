#ifndef KLIB_H__
#define KLIB_H__

#include <am.h>
#include <stddef.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

//#define __NATIVE_USE_KLIB__

// string.h
void  *memset    (void *s, int c, size_t n);
void  *memcpy    (void *dst, const void *src, size_t n);
void  *memmove   (void *dst, const void *src, size_t n);
int    memcmp    (const void *s1, const void *s2, size_t n);
size_t strlen    (const char *s);
char  *strcat    (char *dst, const char *src);
char  *strcpy    (char *dst, const char *src);
char  *strncpy   (char *dst, const char *src, size_t n);
int    strcmp    (const char *s1, const char *s2);
int    strncmp   (const char *s1, const char *s2, size_t n);

// stdlib.h
void   srand     (unsigned int seed);
int    rand      (void);
void  *malloc    (size_t size);
void   free      (void *ptr);
int    abs       (int x);
int    atoi      (const char *nptr);

// stdio.h
// AM builds with -fno-builtin, so GCC no longer knows these are printf-like.
// The format attribute brings back -Wformat checking of the arguments
// (args are checked from the index after the format; 0 = a va_list).
// Spelled __printf__ (like glibc's headers) so a macro named printf can't break it.
#define KLIB_PRINTF(fmt_idx, args_idx) __attribute__((format(__printf__, fmt_idx, args_idx)))
int    printf    (const char *format, ...) KLIB_PRINTF(1, 2);
int    sprintf   (char *str, const char *format, ...) KLIB_PRINTF(2, 3);
int    snprintf  (char *str, size_t size, const char *format, ...) KLIB_PRINTF(3, 4);
int    vsprintf  (char *str, const char *format, va_list ap) KLIB_PRINTF(2, 0);
int    vsnprintf (char *str, size_t size, const char *format, va_list ap) KLIB_PRINTF(3, 0);

// assert.h
#ifdef NDEBUG
  #define assert(ignore) ((void)0)
#else
  #define assert(cond) \
    do { \
      if (!(cond)) { \
        printf("Assertion fail at %s:%d\n", __FILE__, __LINE__); \
        halt(1); \
      } \
    } while (0)
#endif

#ifdef __cplusplus
}
#endif

#endif
