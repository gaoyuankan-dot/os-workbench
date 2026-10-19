#include <klib.h>
#include <klib-macros.h>
#include <stdint.h>

#if !defined(__ISA_NATIVE__) || defined(__NATIVE_USE_KLIB__)

// Byte-at-a-time on purpose: short, obviously correct, and fast enough for
// AM. AM compiles with -fno-builtin, which keeps GCC from "optimizing" these
// loops back into calls to memset/memcpy (that would recurse forever).

size_t strlen(const char *s) {
  const char *p = s;
  while (*p != '\0') p++;
  return (size_t)(p - s);
}

char *strcpy(char *dst, const char *src) {
  char *d = dst;
  while ((*d++ = *src++) != '\0') ;  // copies the '\0' too, then stops
  return dst;
}

// Easy to misremember: copies at most n bytes, fills the rest of dst with
// '\0', and does NOT terminate dst when strlen(src) >= n.
char *strncpy(char *dst, const char *src, size_t n) {
  size_t i = 0;
  for (; i < n && src[i] != '\0'; i++) dst[i] = src[i];
  for (; i < n; i++) dst[i] = '\0';
  return dst;
}

char *strcat(char *dst, const char *src) {
  strcpy(dst + strlen(dst), src);
  return dst;
}

// The C standard compares as unsigned char. With plain (signed) char on x86,
// "\x80" would wrongly sort before "a".
int strcmp(const char *s1, const char *s2) {
  const unsigned char *a = (const unsigned char *)s1;
  const unsigned char *b = (const unsigned char *)s2;
  while (*a != '\0' && *a == *b) { a++; b++; }
  return (int)*a - (int)*b;
}

int strncmp(const char *s1, const char *s2, size_t n) {
  const unsigned char *a = (const unsigned char *)s1;
  const unsigned char *b = (const unsigned char *)s2;
  for (; n > 0; n--, a++, b++) {
    if (*a != *b)   return (int)*a - (int)*b;
    if (*a == '\0') return 0;        // both strings ended together
  }
  return 0;
}

void *memset(void *s, int c, size_t n) {
  unsigned char *p = s;
  while (n-- > 0) *p++ = (unsigned char)c;   // only the low byte of c is used
  return s;
}

// memcpy may assume the ranges do not overlap; memmove may not.
void *memcpy(void *out, const void *in, size_t n) {
  unsigned char *d = out;
  const unsigned char *s = in;
  while (n-- > 0) *d++ = *s++;
  return out;
}

void *memmove(void *dst, const void *src, size_t n) {
  unsigned char *d = dst;
  const unsigned char *s = src;
  // One unsigned compare answers "does dst start inside [src, src + n)?":
  // if dst < src, the subtraction wraps to a huge value, which is >= n.
  // (Comparing pointers into different objects with < is undefined in C;
  // comparing integers is not.)
  if ((uintptr_t)d - (uintptr_t)s >= n) {
    while (n-- > 0) *d++ = *s++;     // no harmful overlap: copy forward
  } else {
    d += n;                          // dst overlaps src's tail: a forward copy
    s += n;                          // would overwrite bytes before reading
    while (n-- > 0) *--d = *--s;     // them, so copy backward instead
  }
  return dst;
}

int memcmp(const void *s1, const void *s2, size_t n) {
  const unsigned char *a = s1;
  const unsigned char *b = s2;
  for (; n > 0; n--, a++, b++) {
    if (*a != *b) return (int)*a - (int)*b;
  }
  return 0;
}

#endif
