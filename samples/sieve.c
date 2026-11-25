#include "silica.h"
#define N 10000
static u8 composite[N + 1];
void _start(void) {
  u64 count = 0;
  for (u64 i = 2; i <= N; ++i) {
    if (!composite[i]) { ++count; for (u64 j = i * i; j <= N; j += i) composite[j] = 1; }
  }
  sil_puts("primes <= 10000: "); sil_putu(count); sil_puts("\n");
  sil_exit(count == 1229 ? 0 : 1);
}
