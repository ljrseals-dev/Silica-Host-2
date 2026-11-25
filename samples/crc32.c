#include "silica.h"
static u32 crc32(const u8* p, u64 n) {
  u32 c = 0xFFFFFFFFu;
  while (n--) { c ^= *p++; for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); }
  return ~c;
}
void _start(void) {
  const char* s = "123456789";
  u32 c = crc32((const u8*)s, 9);
  sil_puts("crc32(\"123456789\") = "); sil_putu(c); sil_puts("\n");
  sil_exit(c == 0xCBF43926u ? 0 : 1);
}
