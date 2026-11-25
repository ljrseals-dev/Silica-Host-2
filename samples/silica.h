/* Bare-metal runtime for SilicaHost guests: hostcalls via BRK #0x5110 (see core/wasm/silica_abi.h). */
#pragma once
typedef unsigned long u64;
typedef unsigned int u32;
typedef unsigned char u8;

static inline u64 silica_hostcall(u64 op, u64 a, u64 b) {
  register u64 x0 __asm__("x0") = op;
  register u64 x1 __asm__("x1") = a;
  register u64 x2 __asm__("x2") = b;
  __asm__ volatile("brk #0x5110" : "+r"(x0) : "r"(x1), "r"(x2) : "memory");
  return x0;
}
static inline void sil_exit(u64 code) { silica_hostcall(0, code, 0); for (;;) {} }
static inline void sil_write(const char* s, u64 n) {
  while (n) { u64 w = silica_hostcall(2, (u64)s, n); s += w; n -= w; }
}
static inline u64 sil_strlen(const char* s) { u64 n = 0; while (s[n]) ++n; return n; }
static inline void sil_puts(const char* s) { sil_write(s, sil_strlen(s)); }
static inline void sil_putu(u64 v) {
  char buf[24]; int i = 24;
  do { buf[--i] = (char)('0' + v % 10); v /= 10; } while (v);
  sil_write(buf + i, (u64)(24 - i));
}
