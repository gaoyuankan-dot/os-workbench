#include <am.h>
#include <klib.h>
#include <klib-macros.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>

#if !defined(__ISA_NATIVE__) || defined(__NATIVE_USE_KLIB__)

// Design: ONE formatter, format(), feeds characters to a Sink. printf's sink
// writes to the serial port (putch); s(n)printf's sink fills a buffer. Every
// public function below is a thin wrapper, so there is one place to get right.
//
// Supported: %d %i %u %x %X %o %c %s %p %%
//   flags  - 0 + space #     width/precision: digits or *
//   length hh h l ll z t j
// Not supported: floating point (%f %e %g). AM builds with -mno-sse, so the
// kernel has no floats to print anyway.

#define MAX_FIELD 4096  // cap on width/precision: a garbage format can't overflow int

// ---------------------------------------------------------------- output sink

typedef struct {
  bool   to_console;  // true: putch() each char; false: store into buf
  char  *buf;
  size_t cap;         // capacity of buf, including room for the final '\0'
  size_t len;         // chars produced so far. Keeps counting past cap,
                      // because snprintf must return the untruncated length.
} Sink;

static void emit(Sink *s, char c) {
  if (s->to_console)            putch(c);
  else if (s->len + 1 < s->cap) s->buf[s->len] = c;  // last byte stays for '\0'
  s->len++;
}

static void emit_repeat(Sink *s, char c, int n) {
  for (; n > 0; n--) emit(s, c);
}

// ------------------------------------------------- conversion spec, e.g. %-08.3lld

typedef enum { LEN_NONE, LEN_HH, LEN_H, LEN_L, LEN_LL, LEN_Z, LEN_T, LEN_J } Length;

typedef struct {
  bool left;   // '-' : pad on the right instead of the left
  bool zero;   // '0' : pad numbers with zeros instead of spaces
  bool alt;    // '#' : "0x" prefix for %x
  char sign;   // '+' or ' ' : what to print before a non-negative number (0 = nothing)
  int  width;  // minimum field width
  int  prec;   // precision; -1 means "not given"
} Spec;

// Read a decimal number from the format and advance past it.
static int parse_num(const char **pp) {
  int v = 0;
  for (; **pp >= '0' && **pp <= '9'; (*pp)++) {
    if (v < MAX_FIELD) v = v * 10 + (**pp - '0');
  }
  return v < MAX_FIELD ? v : MAX_FIELD;
}

static Length parse_length(const char **pp) {
  const char *p = *pp;
  Length len = LEN_NONE;
  switch (*p) {  // p[1] is safe to read: p[0] is a letter, so p[1] is at worst '\0'
    case 'h': if (p[1] == 'h') { len = LEN_HH; p += 2; } else { len = LEN_H; p++; } break;
    case 'l': if (p[1] == 'l') { len = LEN_LL; p += 2; } else { len = LEN_L; p++; } break;
    case 'z': len = LEN_Z; p++; break;
    case 't': len = LEN_T; p++; break;
    case 'j': len = LEN_J; p++; break;
    default:  break;
  }
  *pp = p;
  return len;
}

// ------------------------------------------------------------- argument fetch
// va_arg must name the type the CALLER passed. Types narrower than int
// (char, short) arrive promoted to int, so read an int and narrow it again.
// `va_list *` works here because format() passes the address of a LOCAL
// va_list (see the note in format()).

static long long arg_signed(va_list *ap, Length len) {
  switch (len) {
    case LEN_HH: return (signed char)va_arg(*ap, int);
    case LEN_H:  return (short)va_arg(*ap, int);
    case LEN_L:  return va_arg(*ap, long);
    case LEN_LL: return va_arg(*ap, long long);
    case LEN_Z:  return (ptrdiff_t)va_arg(*ap, size_t);  // %zd: signed size_t
    case LEN_T:  return va_arg(*ap, ptrdiff_t);
    case LEN_J:  return va_arg(*ap, intmax_t);
    default:     return va_arg(*ap, int);
  }
}

static unsigned long long arg_unsigned(va_list *ap, Length len) {
  switch (len) {
    case LEN_HH: return (unsigned char)va_arg(*ap, unsigned int);
    case LEN_H:  return (unsigned short)va_arg(*ap, unsigned int);
    case LEN_L:  return va_arg(*ap, unsigned long);
    case LEN_LL: return va_arg(*ap, unsigned long long);
    case LEN_Z:  return va_arg(*ap, size_t);
    case LEN_T:  return (size_t)va_arg(*ap, ptrdiff_t);
    case LEN_J:  return va_arg(*ap, uintmax_t);
    default:     return va_arg(*ap, unsigned int);
  }
}

// ------------------------------------------------------------------ emitters

// Emit n chars of str, space-padded to the field width.
static void emit_field(Sink *s, const Spec *sp, const char *str, int n) {
  int pad = sp->width > n ? sp->width - n : 0;
  if (!sp->left) emit_repeat(s, ' ', pad);
  for (int i = 0; i < n; i++) emit(s, str[i]);
  if (sp->left) emit_repeat(s, ' ', pad);
}

// Layout of a number: [spaces][sign][prefix][zeros][digits][spaces]
static void emit_number(Sink *s, const Spec *sp, unsigned long long mag, bool neg,
                        unsigned base, bool upper, const char *prefix) {
  const char *set = upper ? "0123456789ABCDEF" : "0123456789abcdef";
  char digits[24];  // UINT64_MAX needs 20 decimal or 22 octal digits
  int n = 0;
  // C rule: "%.0d" with value 0 prints no digits at all.
  if (!(mag == 0 && sp->prec == 0)) {
    do {                              // do-while: the value 0 still prints "0"
      digits[n++] = set[mag % base];  // digits come out least-significant first
      mag /= base;
    } while (mag != 0);
  }

  char sign  = neg ? '-' : sp->sign;
  int  plen  = (int)strlen(prefix);
  int  zeros = sp->prec > n ? sp->prec - n : 0;  // precision = minimum digit count
  int  body  = (sign ? 1 : 0) + plen + zeros + n;
  int  pad   = sp->width > body ? sp->width - body : 0;
  // '0' flag: zeros fill the width *after* the sign ("-0042", not "00-42").
  // C says to ignore it with '-' or with an explicit precision.
  if (sp->zero && !sp->left && sp->prec < 0) { zeros += pad; pad = 0; }

  if (!sp->left) emit_repeat(s, ' ', pad);
  if (sign) emit(s, sign);
  for (const char *p = prefix; *p != '\0'; p++) emit(s, *p);
  emit_repeat(s, '0', zeros);
  while (n > 0) emit(s, digits[--n]);  // reverse back to most-significant first
  if (sp->left) emit_repeat(s, ' ', pad);
}

// ---------------------------------------------------------------- the engine

static void format(Sink *s, const char *fmt, va_list ap_in) {
  if (fmt == NULL) return;
  // Copy ap into a local before taking its address. On x86_64, va_list is an
  // array type; as a *parameter* it decays to a pointer, so &ap_in would have
  // the wrong type for va_list *. A local va_list does not decay.
  va_list ap;
  va_copy(ap, ap_in);

  for (const char *p = fmt; *p != '\0'; p++) {
    if (*p != '%') { emit(s, *p); continue; }
    const char *start = p++;  // remember the '%', to echo unknown specs verbatim

    // 1. flags (any order, any count)
    Spec sp = { .left = false, .zero = false, .alt = false, .sign = 0, .width = 0, .prec = -1 };
    for (;; p++) {
      if      (*p == '-') sp.left = true;
      else if (*p == '0') sp.zero = true;
      else if (*p == '#') sp.alt = true;
      else if (*p == '+') sp.sign = '+';
      else if (*p == ' ') { if (sp.sign != '+') sp.sign = ' '; }  // '+' wins over ' '
      else break;
    }
    // 2. width: digits, or '*' = take it from the argument list
    if (*p == '*') {
      int w = va_arg(ap, int);
      if (w < 0) { sp.left = true; w = -w; }  // C: negative '*' width means '-' flag
      sp.width = w < MAX_FIELD ? w : MAX_FIELD;
      p++;
    } else {
      sp.width = parse_num(&p);
    }
    // 3. precision
    if (*p == '.') {
      p++;
      if (*p == '*') {
        int pr = va_arg(ap, int);
        sp.prec = pr < 0 ? -1 : (pr < MAX_FIELD ? pr : MAX_FIELD);  // negative = not given
        p++;
      } else {
        sp.prec = parse_num(&p);  // "%.d" means precision 0
      }
    }
    // 4. length modifier
    Length len = parse_length(&p);

    // 5. conversion
    switch (*p) {
      case 'd': case 'i': {
        long long v = arg_signed(&ap, len);
        // Negate in unsigned arithmetic: -LLONG_MIN overflows a long long.
        unsigned long long mag = v < 0 ? -(unsigned long long)v : (unsigned long long)v;
        emit_number(s, &sp, mag, v < 0, 10, false, "");
        break;
      }
      case 'u': case 'x': case 'X': case 'o': {
        unsigned long long v = arg_unsigned(&ap, len);
        unsigned base = (*p == 'u') ? 10 : (*p == 'o') ? 8 : 16;
        const char *prefix = (sp.alt && v != 0 && base == 16) ? (*p == 'X' ? "0X" : "0x") : "";
        sp.sign = 0;  // '+' and ' ' apply to signed conversions only
        emit_number(s, &sp, v, false, base, *p == 'X', prefix);
        break;
      }
      case 'p': {
        void *ptr = va_arg(ap, void *);
        sp.sign = 0;
        if (ptr == NULL) emit_field(s, &sp, "(nil)", 5);  // same text as glibc on native
        else             emit_number(s, &sp, (uintptr_t)ptr, false, 16, false, "0x");
        break;
      }
      case 'c': {
        char c = (char)va_arg(ap, int);  // char is promoted to int when passed
        emit_field(s, &sp, &c, 1);
        break;
      }
      case 's': {
        const char *str = va_arg(ap, const char *);
        if (str == NULL) str = "(null)";  // bare metal reads address 0 silently; don't
        int n = 0;                        // precision = max chars, and we must not
        while ((sp.prec < 0 || n < sp.prec) && str[n] != '\0') n++;  // read past it
        emit_field(s, &sp, str, n);
        break;
      }
      case '%':
        emit(s, '%');
        break;
      case '\0':  // format ended in the middle of a spec: echo it, then stop
        for (const char *q = start; q < p; q++) emit(s, *q);
        p--;      // let the loop's p++ land on the '\0' and exit
        break;
      default:    // unknown conversion: echo it verbatim and consume no argument
        for (const char *q = start; q <= p; q++) emit(s, *q);
        break;
    }
  }
  va_end(ap);
}

// ------------------------------------------------------------- public API

static int result(size_t len) {
  return len > INT_MAX ? -1 : (int)len;
}

int vsnprintf(char *out, size_t n, const char *fmt, va_list ap) {
  // out == NULL with n == 0 is the standard "how long would it be?" query.
  Sink s = { .to_console = false, .buf = out, .cap = (out != NULL ? n : 0), .len = 0 };
  format(&s, fmt, ap);
  if (s.cap > 0) out[s.len < s.cap ? s.len : s.cap - 1] = '\0';  // always terminate
  return result(s.len);
}

int vsprintf(char *out, const char *fmt, va_list ap) {
  // No size given: the caller promises the buffer is big enough. This is
  // exactly why snprintf exists; prefer it.
  return vsnprintf(out, SIZE_MAX, fmt, ap);
}

int snprintf(char *out, size_t n, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int r = vsnprintf(out, n, fmt, ap);
  va_end(ap);
  return r;
}

int sprintf(char *out, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int r = vsprintf(out, fmt, ap);
  va_end(ap);
  return r;
}

int printf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  Sink s = { .to_console = true, .buf = NULL, .cap = 0, .len = 0 };
  format(&s, fmt, ap);  // straight to the serial port: no buffer, no length limit
  va_end(ap);
  return result(s.len);
}

#endif
