#include "silica.h"
/* Writes 100000 bytes through the 4 KiB console buffer: exercises hostcall back-pressure (partial writes). */
#define N 100000
static char buf[N];
void _start(void) {
  for (u64 i = 0; i < N; ++i) buf[i] = (char)('a' + (i % 26));
  sil_write(buf, N);
  sil_exit(7);
}
