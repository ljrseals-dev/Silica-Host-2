#include "silica.h"
static u64 fib(u64 n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
void _start(void) {
  for (u64 i = 0; i <= 20; i += 5) { sil_puts("fib("); sil_putu(i); sil_puts(") = "); sil_putu(fib(i)); sil_puts("\n"); }
  sil_exit(0);
}
