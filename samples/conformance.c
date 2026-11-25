/* Differential conformance program: built for the host (x86-64) and for AArch64.
 * Both must print identical output. The host build is the oracle (C semantics via the compiler). */
#ifdef HOST
#include <stdio.h>
typedef unsigned long long u64; typedef unsigned int u32; typedef unsigned char u8;
static void emit(const char* n, u64 v) { printf("%s=%llu\n", n, v); }
#else
#include "silica.h"
static void emit(const char* n, u64 v) { sil_puts(n); sil_puts("="); sil_putu(v); sil_puts("\n"); }
#endif
typedef signed char s8; typedef short s16; typedef int s32; typedef long long s64;

static u64 rng = 0x9E3779B97F4A7C15ull;
static u64 next(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }
#define N 64
static u64 A[N], B[N];
static u32 W[N]; static s32 SW[N]; static s16 SH[N]; static s8 SB[N]; static u8 UB[N]; static u64 R[N];

#define ACC(name, expr) do { u64 acc = 0; for (int i = 0; i < N; ++i) { u64 a = A[i], b = B[i]; (void)a; (void)b; acc = acc * 1000003ull + (u64)(expr); } emit(name, acc); } while (0)

int main_impl(void) {
  for (int i = 0; i < N; ++i) {
    A[i] = next(); B[i] = next();
    if (i % 7 == 0) A[i] = 0; if (i % 11 == 0) B[i] = ~0ull; if (i % 13 == 0) A[i] = 1ull << 63;
    if (i % 5 == 0) { A[i] &= 0xFFFF; B[i] &= 0xFF; }
    W[i] = (u32)A[i]; SW[i] = (s32)B[i]; SH[i] = (s16)A[i]; SB[i] = (s8)B[i]; UB[i] = (u8)A[i];
  }
  /* bitfield / shifts */
  ACC("lsl_c", a << 13); ACC("lsr_c", a >> 7); ACC("asr_c", (u64)((s64)a >> 19));
  ACC("lsl32", (u32)a << 5); ACC("lsr32", (u32)a >> 9); ACC("asr32", (u32)((s32)a >> 3));
  ACC("lsl_v", a << (b & 63)); ACC("lsr_v", a >> (b & 63)); ACC("asr_v", (u64)((s64)a >> (b & 63)));
  ACC("ubfx", (a >> 12) & 0xFFFF); ACC("sbfx", (u64)((s64)(a << 40) >> 52));
  ACC("ubfx32", ((u32)a >> 4) & 0x3FF); ACC("sbfx32", (u32)((s32)((u32)a << 11) >> 21));
  ACC("bfi", (a & ~(0xFFull << 20)) | ((b & 0xFF) << 20));
  ACC("sxtb", (u64)(s64)(s8)a); ACC("sxth", (u64)(s64)(s16)a); ACC("sxtw", (u64)(s64)(s32)a);
  ACC("uxtb", (u8)a); ACC("uxth", (unsigned short)a); ACC("uxtw", (u32)a);
  ACC("sxtb32", (u32)(s32)(s8)a); ACC("sxth32", (u32)(s32)(s16)a);
  ACC("ror64", (a >> 17) | (a << 47)); ACC("ror32", ((u32)a >> 9) | ((u32)a << 23));
  ACC("extr64", (a >> 21) | (b << 43)); ACC("extr32", ((u32)a >> 5) | ((u32)b << 27));
  /* multiplies / divides */
  ACC("umulh", (u64)(((unsigned __int128)a * b) >> 64));
  ACC("smulh", (u64)(((__int128)(s64)a * (s64)b) >> 64));
  ACC("umull", (u64)(u32)a * (u32)b); ACC("smull", (u64)((s64)(s32)a * (s64)(s32)b));
  ACC("umaddl", b + (u64)(u32)a * (u32)b); ACC("smsubl", b - (u64)((s64)(s32)a * (s64)(s32)b));
  ACC("mul", a * b); ACC("mul32", (u32)a * (u32)b);
  ACC("udiv", b ? a / b : 0); ACC("sdiv", (b && !((s64)a == (s64)(1ull << 63) && (s64)b == -1)) ? (u64)((s64)a / (s64)b) : 0);
  ACC("udiv_c", a / 10); ACC("urem_c", a % 1000); ACC("sdiv_c", (u64)((s64)a / 7)); ACC("srem_c", (u64)((s64)a % 13));
  /* bit manipulation */
  ACC("clz", a ? (u64)__builtin_clzll(a) : 64); ACC("clz32", (u32)a ? (u64)__builtin_clz((u32)a) : 32);
  ACC("ctz", a ? (u64)__builtin_ctzll(a) : 64); ACC("popc", (u64)__builtin_popcountll(a));
  ACC("bswap64", __builtin_bswap64(a)); ACC("bswap32", __builtin_bswap32((u32)a)); ACC("bswap16", __builtin_bswap16((unsigned short)a));
  ACC("cls", (s64)a < 0 ? (u64)(a == ~0ull ? 63 : __builtin_clzll(~a) - 1) : (a ? (u64)__builtin_clzll(a) - 1 : 63));
  /* compares / conditional */
  ACC("ccmp", (a < b && (s64)a > (s64)b) ? 1 : 2); ACC("ccmp2", (a == b || (s32)a < (s32)b) ? 3 : 4);
  ACC("csel", (s64)a < (s64)b ? a : b); ACC("cinc", a + (a > b)); ACC("cneg", (s64)a < 0 ? -a : a);
  ACC("addcarry", a + b < a); ACC("subborrow", a - b > a);
  ACC("add_ext", a + (u64)(u32)b); ACC("add_sxt", a + (u64)(s64)(s32)b); ACC("add_uxtb_sh", a + ((u64)(u8)b << 2));
  /* memory: register-offset forms (UXTW / SXTW / LSL idx), sign-extending loads, pairs */
  for (int i = 0; i < N; ++i) R[i] = 0;
  { u64 acc = 0; for (u32 i = 0; i < N; ++i) { R[i] = A[(i * 7) & 63] ^ B[i]; acc += R[i] * (i + 1); } emit("st_x_uxtw", acc); }
  { u64 acc = 0; for (s32 i = 0; i < N; ++i) acc += (u64)(s64)SW[i] + (u64)(s64)SH[i] * 3 + (u64)(s64)SB[i] * 5 + UB[i]; emit("ld_signed", acc); }
  { u64 acc = 0; for (u64 i = 0; i < N; ++i) acc = acc * 31 + W[i] + SH[N - 1 - i]; emit("ld_lsl", acc); }
  { u64 acc = 0; for (s64 i = N - 1; i >= 0; --i) acc ^= (u64)SW[i] << (i & 31); emit("ld_neg", acc); }
  { for (int i = 0; i < N; ++i) { SB[i] = (s8)(A[i] >> 3); SH[i] = (s16)(B[i] >> 5); } u64 acc = 0; for (int i = 0; i < N; ++i) acc = acc * 131 + (u64)(s64)SB[i] + (u64)(s64)SH[i]; emit("st_small", acc); }
  { struct P { s32 x, y; } ps[N / 2], qs[N / 2]; for (int i = 0; i < N / 2; ++i) { ps[i].x = SW[2 * i]; ps[i].y = SW[2 * i + 1]; } for (int i = 0; i < N / 2; ++i) qs[i] = ps[N / 2 - 1 - i];
    u64 acc = 0; for (int i = 0; i < N / 2; ++i) acc = acc * 17 + (u64)(s64)qs[i].x - (u64)(s64)qs[i].y; emit("pairs", acc); }
  return 0;
}
#ifdef HOST
int main(void) { return main_impl(); }
#else
void _start(void) { main_impl(); sil_exit(0); }
#endif
