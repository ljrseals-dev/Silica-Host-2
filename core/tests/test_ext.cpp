// Known-answer tests for the instructions SilicaHost added to the core (see docs/CORE_CHANGES.md).
// Encodings are from llvm-mc (independent of src/asm.hpp); expected values are from Arm ARM definitions.
#include <cstdio>
#include <initializer_list>
#include <vector>
#include "silica/cpu.hpp"

using namespace silica;
static int g_fail = 0, g_checks = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_fail; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
#define EQ(a, b) do { ++g_checks; unsigned long long _a = (a), _b = (b); if (_a != _b) { ++g_fail; \
  std::printf("FAIL %s:%d  %s == %s  (0x%llx vs 0x%llx)\n", __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)

static constexpr uint64_t BASE = 0x1000, DATA = 0x8000;
static Instruction dec(uint32_t w) { Instruction i; decode_into(w, i); return i; }
// Run one instruction with x1,x2,x4,x5 preset (and x0 = x0v, sp = 0x9000). Optional pre-hook seeds memory/flags.
template <class F>
static Cpu go(uint32_t w, uint64_t x1, uint64_t x2, uint64_t x0v, F&& pre) {
  Cpu c;
  c.regs.x[0] = x0v; c.regs.x[1] = x1; c.regs.x[2] = x2; c.regs.x[4] = DATA; c.regs.x[5] = x2; c.regs.sp = 0x9000;
  pre(c);
  std::vector<uint32_t> v{w, 0};
  c.load_program(BASE, v);
  c.run(10);
  return c;
}
static Cpu go(uint32_t w, uint64_t x1 = 0, uint64_t x2 = 0, uint64_t x0v = 0) { return go(w, x1, x2, x0v, [](Cpu&) {}); }
static unsigned nzcv(const Cpu& c) { return c.regs.pstate.nzcv(); }

int main() {
  // ---- one-source: REV / CLZ / CLS / RBIT
  EQ(go(0xDAC00420, 0x0102030405060708ull).regs.x[0], 0x0201040306050807ull);  // rev16 x
  EQ(go(0xDAC00820, 0x0102030405060708ull).regs.x[0], 0x0403020108070605ull);  // rev32 x
  EQ(go(0x5AC00820, 0x11223344ull).regs.x[0], 0x44332211ull);                  // rev w
  EQ(go(0xDAC00C20, 0x0102030405060708ull).regs.x[0], 0x0807060504030201ull);  // rev x
  EQ(go(0xDAC01420, 0xFFFFFFFFFFFFFF00ull).regs.x[0], 55);                      // cls x
  EQ(go(0xDAC01420, 0).regs.x[0], 63); EQ(go(0xDAC01420, 1).regs.x[0], 62); EQ(go(0xDAC01420, ~0ull).regs.x[0], 63);
  EQ(go(0x5AC01420, 0xFFFF0000ull).regs.x[0], 15);                              // cls w
  EQ(go(0x5AC01020, 0).regs.x[0], 32); EQ(go(0x5AC01020, 0x10000).regs.x[0], 15);  // clz w
  EQ(go(0xDAC01020, 1).regs.x[0], 63); EQ(go(0xDAC01020, 0).regs.x[0], 64);     // clz x
  EQ(go(0xDAC00020, 1).regs.x[0], 0x8000000000000000ull);                       // rbit x
  EQ(go(0x5AC00020, 1).regs.x[0], 0x80000000ull); EQ(go(0x5AC00020, 0xFFFF).regs.x[0], 0xFFFF0000ull);  // rbit w
  // ---- conditional compare
  { Cpu c = go(0xBA450822, 0xFFFFFFFFFFFFFFFBull, 0, 0, [](Cpu& c) { c.regs.pstate.z = true; });  // ccmn x1,#5,#2,eq (holds): -5+5=0
    EQ(nzcv(c), 0b0110); }
  { Cpu c = go(0xBA450822, 7, 0, 0, [](Cpu& c) { c.regs.pstate.z = false; }); EQ(nzcv(c), 0b0010); }  // cond fails -> #2
  { Cpu c = go(0xFA421028, 5, 5); EQ(nzcv(c), 0b0110); }                                              // ccmp x1,x2,#8,ne : 5-5
  { Cpu c = go(0xFA421028, 5, 5, 0, [](Cpu& c) { c.regs.pstate.z = true; }); EQ(nzcv(c), 0b1000); }   // ne fails -> #8
  { Cpu c = go(0x7A438820, 2, 0, 0, [](Cpu& c) { c.regs.pstate.c = true; c.regs.pstate.z = false; }); EQ(nzcv(c), 0b1000); }  // hi: 2-3
  // ---- loads/stores: LDPSW, LDNP/STNP, register offset
  { Cpu c = go(0x69400C80, 0, 0, 0, [](Cpu& c) { c.mem.write<uint32_t>(DATA, 0xFFFFFFFEu); c.mem.write<uint32_t>(DATA + 4, 5); });
    EQ(c.regs.x[0], 0xFFFFFFFFFFFFFFFEull); EQ(c.regs.x[3], 5); }
  { Cpu c = go(0x69C10C80, 0, 0, 0, [](Cpu& c) { c.mem.write<uint32_t>(DATA + 8, 0x80000000u); c.mem.write<uint32_t>(DATA + 12, 7); });
    EQ(c.regs.x[0], 0xFFFFFFFF80000000ull); EQ(c.regs.x[3], 7); EQ(c.regs.x[4], DATA + 8); }          // pre-index writes back
  { Cpu c = go(0xA8400C80, 0, 0, 0, [](Cpu& c) { c.mem.write<uint64_t>(DATA, 0x1111); c.mem.write<uint64_t>(DATA + 8, 0x2222); });
    EQ(c.regs.x[0], 0x1111); EQ(c.regs.x[3], 0x2222); }                                               // ldnp
  { Cpu c = go(0xA8000C80, 0, 0, 0x33, [](Cpu& c) { c.regs.x[3] = 0x44; }); EQ(c.mem.read<uint64_t>(DATA), 0x33); EQ(c.mem.read<uint64_t>(DATA + 8), 0x44); }  // stnp
  { Cpu c = go(0xF8655880, 0, 0xFFFFFFFF00000002ull, 0, [](Cpu& c) { c.mem.write<uint64_t>(DATA + 16, 0xAA55); });
    EQ(c.regs.x[0], 0xAA55); CHECK(!c.mem.fault()); }                                                  // ldr x0,[x4,w5,uxtw #3] (uses w5 only)
  { Cpu c = go(0xB865D880, 0, 0xFFFFFFFEull, 0, [](Cpu& c) { c.mem.write<uint32_t>(DATA - 8, 0x12345678u); });
    EQ(c.regs.x[0], 0x12345678ull); }                                                                  // ldr w0,[x4,w5,sxtw #2] (-2 -> -8)
  { Cpu c = go(0x38A56880, 0, 3, 0, [](Cpu& c) { c.mem.write<uint8_t>(DATA + 3, 0x80); }); EQ(c.regs.x[0], 0xFFFFFFFFFFFFFF80ull); }  // ldrsb x0,[x4,x5]
  { Cpu c = go(0x78E57880, 0, 2, 0, [](Cpu& c) { c.mem.write<uint16_t>(DATA + 4, 0x8001); }); EQ(c.regs.x[0], 0xFFFF8001ull); }       // ldrsh w0,[x4,x5,lsl #1]
  { Cpu c = go(0x78257880, 0, 3, 0xABCD1234ull); EQ(c.mem.read<uint16_t>(DATA + 6), 0x1234); }          // strh w0,[x4,x5,lsl #1]
  // ---- extended-register add/sub
  EQ(go(0x8B214BE0, 0xFFFFFFFF00000010ull).regs.x[0], 0x9040);                  // add x0,sp,w1,uxtw #2
  EQ(go(0xCB228420, 100, 0x80).regs.x[0], 356);                                 // sub x0,x1,w2,sxtb #1  (100 - (-128<<1))
  { Cpu c = go(0xAB22A020, 1, 0xFFFF); EQ(c.regs.x[0], 0); EQ(nzcv(c), 0b0110); }  // adds x0,x1,w2,sxth
  { Cpu c = go(0xEB22003F, 0x12, 0x112); EQ(nzcv(c), 0b0110); }                 // cmp x1,w2,uxtb
  EQ(go(0x0B228820, 1, 0xFF).regs.x[0], 0xFFFFFFFDull);                         // add w0,w1,w2,sxtb #2
  // ---- extract / bitfield family
  EQ(go(0x93C23020, 1, 0x1000).regs.x[0], 0x0010000000000001ull);               // extr x0,x1,x2,#12
  EQ(go(0x13821420, 1, 0x20).regs.x[0], 0x08000001ull);                         // extr w0,w1,w2,#5
  EQ(go(0x93C12420, 0x200).regs.x[0], 1); EQ(go(0x93C12420, 1).regs.x[0], 0x0080000000000000ull);  // ror x0,x1,#9
  EQ(go(0xD3485C20, 0x123456789Aull).regs.x[0], 0x5678);                        // ubfx x0,x1,#8,#16
  EQ(go(0x93485C20, 0x800000).regs.x[0], 0xFFFFFFFFFFFF8000ull);                // sbfx x0,x1,#8,#16
  EQ(go(0x13042C20, 0x800).regs.x[0], 0xFFFFFF80ull);                           // sbfx w0,w1,#4,#8
  EQ(go(0xD3744C20, 0xFFFFFFFFull).regs.x[0], 0xFFFFF000ull);                   // ubfiz x0,x1,#12,#20
  EQ(go(0xB3701C20, 0xAB, 0, ~0ull).regs.x[0], 0xFFFFFFFFFFABFFFFull);          // bfi x0,x1,#16,#8
  EQ(go(0xB3443C20, 0x1234ABCDull, 0, ~0ull).regs.x[0], 0xFFFFFFFFFFFFFABCull); // bfxil x0,x1,#4,#12 (x1[15:4]=0xABC)
  EQ(go(0xB3780FE0, 0, 0, 0xFFFF).regs.x[0], 0xF0FF);                           // bfc x0,#8,#4
  EQ(go(0xD37CEC20, 0x0F).regs.x[0], 0xF0);                                     // lsl x0,x1,#4
  EQ(go(0x53037C20, 0xFFFFFFFFull).regs.x[0], 0x1FFFFFFFull);                   // lsr w0,w1,#3
  EQ(go(0x937CFC20, 0x8000000000000000ull).regs.x[0], 0xFFFFFFFFFFFFFFF8ull);   // asr x0,x1,#60
  EQ(go(0x93401C20, 0x80).regs.x[0], 0xFFFFFFFFFFFFFF80ull);                    // sxtb x0,w1
  EQ(go(0x13003C20, 0x8000).regs.x[0], 0xFFFF8000ull);                          // sxth w0,w1
  EQ(go(0x93407C20, 0x80000000ull).regs.x[0], 0xFFFFFFFF80000000ull);           // sxtw x0,w1
  EQ(go(0x53001C20, 0x1FF).regs.x[0], 0xFF); EQ(go(0x53003C20, 0x1FFFF).regs.x[0], 0xFFFF);  // uxtb / uxth
  // ---- widening / high multiplies
  EQ(go(0x9B427C20, ~0ull, ~0ull).regs.x[0], 0);                                // smulh (-1*-1 = 1 -> hi 0)
  EQ(go(0x9B427C20, 0x8000000000000000ull, 2).regs.x[0], ~0ull);                // smulh (-2^63*2 = -2^64 -> hi -1)
  EQ(go(0x9BC27C20, ~0ull, ~0ull).regs.x[0], 0xFFFFFFFFFFFFFFFEull);           // umulh
  EQ(go(0x9B227C20, 0xFFFFFFFFull, 2).regs.x[0], 0xFFFFFFFFFFFFFFFEull);        // smull (-1*2)
  EQ(go(0x9BA27C20, 0xFFFFFFFFull, 2).regs.x[0], 0x1FFFFFFFEull);               // umull
  EQ(go(0x9B22FC20, 0xFFFFFFFFull, 2).regs.x[0], 2);                            // smnegl
  { Cpu c = go(0x9BA20C20, 0xFFFFFFFFull, 2, 0, [](Cpu& c) { c.regs.x[3] = 10; }); EQ(c.regs.x[0], 0x200000008ull); }  // umaddl
  // ---- reserved / unallocated encodings must stay Invalid
  CHECK(dec(0xD3085C20).code == Code::Invalid);  // UBFM sf=1 with N=0
  CHECK(dec(0x13C21420).code == Code::Invalid);  // EXTR sf=0 with N=1
  CHECK(dec(0xF3485C20).code == Code::Invalid);  // bitfield opc=3
  CHECK(dec(0x8B2157E0).code == Code::Invalid);  // add (extended) imm3 > 4
  CHECK(dec(0xF8650880).code == Code::Invalid);  // ldr register offset, option=UXTB (reserved)
  CHECK(dec(0x69000C80).code == Code::Invalid);  // opc=01,L=0 is STGP (MTE), not modelled
  CHECK(dec(0x9B227C20).code == Code::Smaddl);   // op31=001 must decode as SMADDL, not alias MADD
  CHECK(dec(0x9B427C20 | (1u << 15)).code == Code::Invalid);  // SMULH requires o0=0
  CHECK(dec(0x1BA20C20).code == Code::Invalid);  // umaddl with sf=0 is unallocated
  std::printf("%d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
