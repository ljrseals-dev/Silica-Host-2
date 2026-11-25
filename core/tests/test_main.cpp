// Dependency-free test runner. Build: make test
#include <cstdio>
#include <initializer_list>
#include <vector>
#include "silica/asm.hpp"
#include "silica/cpu.hpp"

using namespace silica;
namespace e = silica::enc;

static int g_fail = 0, g_checks = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_fail; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_EQ(a, b) do { ++g_checks; auto _a = (a); auto _b = (b); if (!(_a == _b)) { ++g_fail; \
  std::printf("FAIL %s:%d  %s == %s  (0x%llx vs 0x%llx)\n", __FILE__, __LINE__, #a, #b, \
  (unsigned long long)_a, (unsigned long long)_b); } } while (0)

static constexpr uint64_t BASE = 0x1000;

static void run(Cpu& c, std::initializer_list<uint32_t> prog, uint64_t max = 100000) {
  std::vector<uint32_t> v(prog);
  v.push_back(0);  // halt word
  c.load_program(BASE, v);
  c.run(max);
}
static Instruction dec(uint32_t w) { Instruction i; decode_into(w, i); return i; }
struct Flags { bool n, z, c, v; };
static void CHECK_FLAGS(const Cpu& c, Flags f, int line) {
  ++g_checks;
  const auto& p = c.regs.pstate;
  if (p.n != f.n || p.z != f.z || p.c != f.c || p.v != f.v) {
    ++g_fail;
    std::printf("FAIL line %d flags NZCV=%d%d%d%d expected %d%d%d%d\n", line, p.n, p.z, p.c, p.v, f.n, f.z, f.c, f.v);
  }
}
#define FLAGS(cpu, n, z, c, v) CHECK_FLAGS(cpu, Flags{n, z, c, v}, __LINE__)

// One flag-setting op: set x1/x2, run `insn`, expose result in x0.
static Cpu op2(uint32_t insn, uint64_t x1, uint64_t x2, bool carry_in = false) {
  Cpu c;
  c.regs.x[1] = x1; c.regs.x[2] = x2; c.regs.pstate.c = carry_in;
  c.regs.pstate.v = true;  // ensure logical ops visibly clear V/C
  run(c, {insn});
  return c;
}

static void test_known_encodings() {
  // Words cross-checked against real assembler output (independent of the encoders).
  CHECK(dec(0xD503201F).code == Code::Nop);
  CHECK(dec(0xD65F03C0).code == Code::Ret && dec(0xD65F03C0).rn == 30);
  CHECK(dec(0x91000400).code == Code::AddImm && dec(0x91000400).imm == 1);
  CHECK(dec(0xD2800020).code == Code::MovZ && dec(0xD2800020).rd == 0 && dec(0xD2800020).imm == 1);
  CHECK(dec(0xAA0103E0).code == Code::OrrReg && dec(0xAA0103E0).rn == 31 && dec(0xAA0103E0).rm == 1);
  CHECK(dec(0x8B020020).code == Code::AddReg);
  CHECK(dec(0x1AC20820).code == Code::Udiv && dec(0x1AC20820).sf == 0);
  CHECK(dec(0x9B027C20).code == Code::Madd && dec(0x9B027C20).ra == 31);
  CHECK(dec(0xB4000040).code == Code::Cbz && dec(0xB4000040).imm == 8);
  CHECK(dec(0xF9400020).code == Code::Ldr && dec(0xF9400020).size == 3);
  CHECK(dec(0xA9BF7BFD).code == Code::Stp && dec(0xA9BF7BFD).mode == 1 && dec(0xA9BF7BFD).imm == -16);
  CHECK(dec(0xA8C17BFD).code == Code::Ldp && dec(0xA8C17BFD).mode == 2 && dec(0xA8C17BFD).imm == 16);
  CHECK(dec(0x92401C20).code == Code::AndImm && static_cast<uint64_t>(dec(0x92401C20).imm) == 0xFF);
  CHECK(static_cast<uint64_t>(dec(0xB200F3E0).imm) == 0x5555555555555555ull);
  CHECK(dec(0x00000000).code == Code::Halt);
  CHECK(dec(0xFFFFFFFF).code == Code::Invalid);
  CHECK(dec(e::logical_imm(1, 1, 0, 0, 0x3F, 31, 0)).code == Code::Invalid);  // all-ones element reserved
  CHECK(dec(e::logical_imm(0, 1, 1, 0, 7, 31, 0)).code == Code::Invalid);     // N=1 with sf=0
  // Encoders agree with the cross-checked constants.
  CHECK_EQ(e::add_imm(0, 0, 1), 0x91000400u);
  CHECK_EQ(e::ldp_stp(2, 0, 3, -2, 30, 31, 29), 0xA9BF7BFDu);
  CHECK_EQ(e::ldst_uimm(3, 1, 0, 1, 0), 0xF9400020u);
}

static void test_dp_imm() {
  { Cpu c; c.regs.x[1] = 41; run(c, {e::add_imm(0, 1, 1)}); CHECK_EQ(c.regs.x[0], 42u); }
  { Cpu c; c.regs.x[1] = 0; run(c, {e::add_sub_imm(1, 0, 0, 1, 1, 1, 0)}); CHECK_EQ(c.regs.x[0], 4096u); }  // LSL #12
  { Cpu c; c.regs.sp = 0x8000; run(c, {e::sub_imm(31, 31, 16)}); CHECK_EQ(c.regs.sp, 0x7FF0u); }          // SUB sp,sp,#16
  { Cpu c; c.regs.sp = 0x8000; run(c, {e::add_imm(0, 31, 0)}); CHECK_EQ(c.regs.x[0], 0x8000u); }          // MOV x0, sp
  { Cpu c; c.regs.x[1] = 5; run(c, {e::subs_imm(31, 1, 5)}); FLAGS(c, 0, 1, 1, 0); }                       // CMP x1,#5
  { Cpu c; run(c, {e::movz(1, 0x1234, 0, 0), e::movk(1, 0x5678, 1, 0), e::movk(1, 0x9ABC, 2, 0), e::movk(1, 0xDEF0, 3, 0)});
    CHECK_EQ(c.regs.x[0], 0xDEF09ABC56781234ull); }
  { Cpu c; run(c, {e::movn(1, 0, 0, 0)}); CHECK_EQ(c.regs.x[0], ~0ull); }
  { Cpu c; run(c, {e::movn(0, 0, 0, 0)}); CHECK_EQ(c.regs.x[0], 0xFFFFFFFFull); }   // MOVN w0,#0 zero-extends
  { Cpu c; c.regs.x[0] = 0xAAAAAAAAFFFFFFFFull; run(c, {e::movk(0, 0x1234, 1, 0)}); CHECK_EQ(c.regs.x[0], 0x1234FFFFull); }
  { Cpu c; c.regs.x[1] = 0x1234; run(c, {0x92401C20u}); CHECK_EQ(c.regs.x[0], 0x34u); }                    // AND x0,x1,#0xFF
  { Cpu c; run(c, {0xB200F3E0u}); CHECK_EQ(c.regs.x[0], 0x5555555555555555ull); }                         // ORR x0,xzr,#0x5555..
  { Cpu c; c.regs.x[1] = 0xFF; run(c, {e::logical_imm(1, 2, 1, 0, 7, 1, 0)}); CHECK_EQ(c.regs.x[0], 0u); } // EOR x0,x1,#0xFF
  { Cpu c; c.regs.x[1] = 0x100; c.regs.pstate.c = c.regs.pstate.v = true;
    run(c, {e::logical_imm(1, 3, 1, 0, 7, 1, 31)}); FLAGS(c, 0, 1, 0, 0); }                               // TST x1,#0xFF
  { Cpu c; run(c, {0x10000040u /* ADR x0,#8 */}); CHECK_EQ(c.regs.x[0], BASE + 8); }
  { Cpu c; run(c, {0x90000000u | 0u /* ADRP x0,#0 */}); CHECK_EQ(c.regs.x[0], BASE & ~0xFFFull); }
}

static void test_flags() {
  // ADDS x0,x1,x2 (64-bit)
  const uint32_t adds = e::add_sub_reg(1, 0, 1, 0, 2, 0, 1, 0);
  { Cpu c = op2(adds, ~0ull, 1);                     CHECK_EQ(c.regs.x[0], 0u);              FLAGS(c, 0, 1, 1, 0); }
  { Cpu c = op2(adds, 0x7FFFFFFFFFFFFFFFull, 1);     CHECK_EQ(c.regs.x[0], 1ull << 63);      FLAGS(c, 1, 0, 0, 1); }
  { Cpu c = op2(adds, 1ull << 63, 1ull << 63);       CHECK_EQ(c.regs.x[0], 0u);              FLAGS(c, 0, 1, 1, 1); }
  { Cpu c = op2(adds, 1, 2);                         CHECK_EQ(c.regs.x[0], 3u);              FLAGS(c, 0, 0, 0, 0); }
  { Cpu c = op2(adds, 1ull << 63, ~0ull);            CHECK_EQ(c.regs.x[0], 0x7FFFFFFFFFFFFFFFull); FLAGS(c, 0, 0, 1, 1); }
  // SUBS x0,x1,x2 (64-bit)
  const uint32_t subs = e::add_sub_reg(1, 1, 1, 0, 2, 0, 1, 0);
  { Cpu c = op2(subs, 5, 5);                         CHECK_EQ(c.regs.x[0], 0u);              FLAGS(c, 0, 1, 1, 0); }
  { Cpu c = op2(subs, 0, 1);                         CHECK_EQ(c.regs.x[0], ~0ull);           FLAGS(c, 1, 0, 0, 0); }
  { Cpu c = op2(subs, 1ull << 63, 1);                CHECK_EQ(c.regs.x[0], 0x7FFFFFFFFFFFFFFFull); FLAGS(c, 0, 0, 1, 1); }
  { Cpu c = op2(subs, 3, 5);                         CHECK_EQ(c.regs.x[0], ~1ull);           FLAGS(c, 1, 0, 0, 0); }
  { Cpu c = op2(subs, 0x7FFFFFFFFFFFFFFFull, ~0ull); CHECK_EQ(c.regs.x[0], 1ull << 63);      FLAGS(c, 1, 0, 0, 1); }
  // 32-bit forms: upper halves of the sources must be ignored, result zero-extended
  const uint32_t addsw = e::add_sub_reg(0, 0, 1, 0, 2, 0, 1, 0);
  const uint32_t subsw = e::add_sub_reg(0, 1, 1, 0, 2, 0, 1, 0);
  { Cpu c = op2(addsw, 0xDEADBEEF7FFFFFFFull, 1);    CHECK_EQ(c.regs.x[0], 0x80000000ull);   FLAGS(c, 1, 0, 0, 1); }
  { Cpu c = op2(addsw, 0xDEADBEEFFFFFFFFFull, 1);    CHECK_EQ(c.regs.x[0], 0u);              FLAGS(c, 0, 1, 1, 0); }
  { Cpu c = op2(subsw, 0xDEADBEEF80000000ull, 1);    CHECK_EQ(c.regs.x[0], 0x7FFFFFFFull);   FLAGS(c, 0, 0, 1, 1); }
  { Cpu c = op2(subsw, 0, 1);                        CHECK_EQ(c.regs.x[0], 0xFFFFFFFFull);   FLAGS(c, 1, 0, 0, 0); }
  // CMP (Rd = XZR) leaves registers alone
  { Cpu c = op2(e::add_sub_reg(1, 1, 1, 0, 2, 0, 1, 31), 7, 7); CHECK_EQ(c.regs.x[31], 0u); FLAGS(c, 0, 1, 1, 0); }
  // ADCS / SBCS (carry-in matters)
  const uint32_t adcs = e::adc_sbc(1, 0, 1, 2, 1, 0), sbcs = e::adc_sbc(1, 1, 1, 2, 1, 0);
  { Cpu c = op2(adcs, ~0ull, 0, true);               CHECK_EQ(c.regs.x[0], 0u);              FLAGS(c, 0, 1, 1, 0); }
  { Cpu c = op2(adcs, 0x7FFFFFFFFFFFFFFFull, 0, true); CHECK_EQ(c.regs.x[0], 1ull << 63);    FLAGS(c, 1, 0, 0, 1); }
  { Cpu c = op2(adcs, 1, 2, false);                  CHECK_EQ(c.regs.x[0], 3u);              FLAGS(c, 0, 0, 0, 0); }
  { Cpu c = op2(sbcs, 0, 0, false);                  CHECK_EQ(c.regs.x[0], ~0ull);           FLAGS(c, 1, 0, 0, 0); }
  { Cpu c = op2(sbcs, 0, 0, true);                   CHECK_EQ(c.regs.x[0], 0u);              FLAGS(c, 0, 1, 1, 0); }
  { Cpu c = op2(e::adc_sbc(1, 0, 0, 2, 1, 0), 10, 20, true); CHECK_EQ(c.regs.x[0], 31u); }  // ADC (no flags)
  // ANDS clears C and V, sets N/Z from result
  const uint32_t ands = e::logical_reg(1, 3, 0, 0, 2, 0, 1, 0);
  { Cpu c = op2(ands, 1ull << 63, ~0ull, true);      CHECK_EQ(c.regs.x[0], 1ull << 63);      FLAGS(c, 1, 0, 0, 0); }
  { Cpu c = op2(ands, 0xF0, 0x0F, true);             CHECK_EQ(c.regs.x[0], 0u);              FLAGS(c, 0, 1, 0, 0); }
  { Cpu c = op2(e::logical_reg(0, 3, 0, 0, 2, 0, 1, 0), 0x80000000ull, 0xFFFFFFFFull); FLAGS(c, 1, 0, 0, 0); }  // ANDS w
  // Flag-setting does NOT happen without S
  { Cpu c = op2(e::add_sub_reg(1, 0, 0, 0, 2, 0, 1, 0), ~0ull, 1); FLAGS(c, 0, 0, 0, 1); /* V preset true, untouched */ }
}

static void test_dp_reg() {
  { Cpu c = op2(e::add_sub_reg(1, 0, 0, 0, 2, 3, 1, 0), 1, 2);  CHECK_EQ(c.regs.x[0], 17u); }              // ADD x0,x1,x2,LSL #3
  { Cpu c = op2(e::add_sub_reg(1, 1, 0, 1, 2, 4, 1, 0), 100, 0x40); CHECK_EQ(c.regs.x[0], 96u); }          // SUB x0,x1,x2,LSR #4
  { Cpu c = op2(e::add_sub_reg(0, 0, 0, 2, 2, 1, 1, 0), 0, 0x80000000ull); CHECK_EQ(c.regs.x[0], 0xC0000000ull); } // ASR w
  { Cpu c = op2(e::logical_reg(1, 1, 3, 0, 2, 4, 1, 0), 0, 0x1F); CHECK_EQ(c.regs.x[0], 0xF000000000000001ull); }  // ORR ..,ROR #4
  { Cpu c = op2(e::logical_reg(1, 0, 0, 1, 2, 0, 1, 0), 0xFF, 0x0F); CHECK_EQ(c.regs.x[0], 0xF0u); }       // BIC
  { Cpu c = op2(e::logical_reg(1, 1, 0, 1, 2, 0, 31, 0), 0, 0xFF); CHECK_EQ(c.regs.x[0], ~0xFFull); }       // ORN (MVN)
  { Cpu c = op2(e::eor_reg(0, 1, 2, 0), 0xFFFFFFFF00FF00FFull, 0xFF); CHECK_EQ(c.regs.x[0], 0x00FF0000ull); } // EOR w
  { Cpu c = op2(e::dp2(1, 8, 2, 1, 0), 1, 67);                  CHECK_EQ(c.regs.x[0], 8u); }                // LSLV (67 mod 64 = 3)
  { Cpu c = op2(e::dp2(0, 9, 2, 1, 0), 0x80000000ull, 35);      CHECK_EQ(c.regs.x[0], 0x10000000ull); }     // LSRV w (35 mod 32 = 3)
  { Cpu c = op2(e::dp2(1, 10, 2, 1, 0), 1ull << 63, 63);        CHECK_EQ(c.regs.x[0], ~0ull); }             // ASRV
  { Cpu c = op2(e::dp2(1, 11, 2, 1, 0), 1, 1);                  CHECK_EQ(c.regs.x[0], 1ull << 63); }        // RORV
  { Cpu c = op2(e::udiv(1, 0, 1, 2), 100, 7);                   CHECK_EQ(c.regs.x[0], 14u); }
  { Cpu c = op2(e::udiv(1, 0, 1, 2), 100, 0);                   CHECK_EQ(c.regs.x[0], 0u); }                // /0 -> 0
  { Cpu c = op2(e::udiv(0, 0, 1, 2), 0xFFFFFFFF00000064ull, 7); CHECK_EQ(c.regs.x[0], 14u); }
  { Cpu c = op2(e::sdiv(1, 0, 1, 2), static_cast<uint64_t>(-100), 7); CHECK_EQ(c.regs.x[0], static_cast<uint64_t>(-14)); }
  { Cpu c = op2(e::sdiv(1, 0, 1, 2), 1ull << 63, ~0ull);       CHECK_EQ(c.regs.x[0], 1ull << 63); }        // MIN / -1
  { Cpu c = op2(e::sdiv(0, 0, 1, 2), 0x80000000ull, 0xFFFFFFFFull); CHECK_EQ(c.regs.x[0], 0x80000000ull); }
  { Cpu c = op2(e::sdiv(1, 0, 1, 2), 5, 0);                     CHECK_EQ(c.regs.x[0], 0u); }
  { Cpu c = op2(e::madd(1, 0, 2, 31, 1, 0), 6, 7);              CHECK_EQ(c.regs.x[0], 42u); }               // MUL
  { Cpu c = op2(e::madd(1, 0, 2, 3, 1, 0), 6, 7);               CHECK_EQ(c.regs.x[0], 42u); }
  { Cpu c; c.regs.x[1] = 6; c.regs.x[2] = 7; c.regs.x[3] = 100; run(c, {e::madd(1, 0, 2, 3, 1, 0)}); CHECK_EQ(c.regs.x[0], 142u); }
  { Cpu c; c.regs.x[1] = 6; c.regs.x[2] = 7; c.regs.x[3] = 100; run(c, {e::madd(1, 1, 2, 3, 1, 0)}); CHECK_EQ(c.regs.x[0], 58u); }  // MSUB
  { Cpu c; c.regs.x[1] = 0xFFFFFFFFull; c.regs.x[2] = 2; run(c, {e::madd(0, 0, 2, 31, 1, 0)}); CHECK_EQ(c.regs.x[0], 0xFFFFFFFEull); }
  // CSEL family after CMP x1,x2 with x1=1,x2=2 -> LT true, GE false
  { Cpu c; c.regs.x[1] = 1; c.regs.x[2] = 2;
    run(c, {e::add_sub_reg(1, 1, 1, 0, 2, 0, 1, 31),
            e::csel_family(1, 0, 0, 2, e::COND_LT, 1, 3),    // CSEL  x3 = LT ? x1 : x2 -> 1
            e::csel_family(1, 0, 1, 2, e::COND_GE, 1, 4),    // CSINC x4 = GE ? x1 : x2+1 -> 3
            e::csel_family(1, 1, 0, 2, e::COND_GE, 1, 5),    // CSINV x5 = ~x2
            e::csel_family(1, 1, 1, 2, e::COND_GE, 1, 6)});  // CSNEG x6 = -x2
    CHECK_EQ(c.regs.x[3], 1u); CHECK_EQ(c.regs.x[4], 3u);
    CHECK_EQ(c.regs.x[5], ~2ull); CHECK_EQ(c.regs.x[6], static_cast<uint64_t>(-2)); }
}

static void test_branches() {
  { Cpu c; run(c, {e::movz(1, 0, 0, 0), e::movz(1, 10, 0, 1),            // sum 1..10
                   e::add_reg(0, 0, 1), e::subs_imm(1, 1, 1), e::b_cond(e::COND_NE, -2)});
    CHECK_EQ(c.regs.x[0], 55u); CHECK_EQ(c.regs.x[1], 0u); CHECK(c.halt == HaltReason::ZeroWord); }
  { Cpu c; run(c, {e::movz(1, 0, 0, 1), e::cbz(1, 0, 2, 1), e::movz(1, 1, 0, 0), e::movz(1, 7, 0, 2)});
    CHECK_EQ(c.regs.x[0], 0u); CHECK_EQ(c.regs.x[2], 7u); }                                               // CBZ taken
  { Cpu c; run(c, {e::movz(1, 1, 0, 1), e::cbz(1, 1, 2, 1), e::movz(1, 1, 0, 0), e::movz(1, 7, 0, 2)});
    CHECK_EQ(c.regs.x[0], 0u); CHECK_EQ(c.regs.x[2], 7u); }                                               // CBNZ taken
  { Cpu c; run(c, {e::movz(1, 1, 0, 1), e::cbz(1, 0, 2, 1), e::movz(1, 1, 0, 0), e::movz(1, 7, 0, 2)});
    CHECK_EQ(c.regs.x[0], 1u); }                                                                          // CBZ not taken
  { Cpu c; c.regs.x[1] = 0x100000000ull; run(c, {e::cbz(0, 0, 2, 1), e::movz(1, 1, 0, 0), e::movz(1, 7, 0, 2)});
    CHECK_EQ(c.regs.x[0], 0u); CHECK_EQ(c.regs.x[2], 7u); }                                               // CBZ w ignores upper half
  { Cpu c; c.regs.x[1] = 4; run(c, {e::tbz(1, 2, 2, 1), e::movz(1, 1, 0, 0), e::movz(1, 7, 0, 2)});
    CHECK_EQ(c.regs.x[0], 0u); }                                                                          // TBNZ bit2 taken
  { Cpu c; c.regs.x[1] = 4; run(c, {e::tbz(0, 2, 2, 1), e::movz(1, 1, 0, 0), e::movz(1, 7, 0, 2)});
    CHECK_EQ(c.regs.x[0], 1u); }                                                                          // TBZ bit2 not taken
  { Cpu c; c.regs.x[1] = 1ull << 40; run(c, {e::tbz(1, 40, 2, 1), e::movz(1, 1, 0, 0), e::movz(1, 7, 0, 2)});
    CHECK_EQ(c.regs.x[0], 0u); }                                                                          // b5=1 form
  { Cpu c; run(c, {e::bl(3), e::movz(1, 5, 0, 1), 0u, e::movz(1, 7, 0, 2), e::ret()});                   // BL / RET
    CHECK_EQ(c.regs.x[30], BASE + 4); CHECK_EQ(c.regs.x[2], 7u); CHECK_EQ(c.regs.x[1], 5u); }
  { Cpu c; c.regs.x[5] = BASE + 12; run(c, {e::blr(5), e::movz(1, 1, 0, 0), 0u, e::movz(1, 9, 0, 2)});   // BLR (target read before link)
    CHECK_EQ(c.regs.x[30], BASE + 4); CHECK_EQ(c.regs.x[2], 9u); }
  { Cpu c; c.regs.x[5] = BASE + 8; run(c, {e::br(5), e::movz(1, 1, 0, 0), e::movz(1, 3, 0, 2)});          // BR
    CHECK_EQ(c.regs.x[0], 0u); CHECK_EQ(c.regs.x[2], 3u); }
  { Cpu c; run(c, {e::b(-0 + 2), e::movz(1, 1, 0, 0), e::movz(1, 3, 0, 2)}); CHECK_EQ(c.regs.x[0], 0u); }  // B
  { Cpu c; run(c, {e::brk(1)}); CHECK(c.halt == HaltReason::Brk); }
  { Cpu c; run(c, {0xFFFFFFFFu}); CHECK(c.halt == HaltReason::Invalid); CHECK_EQ(c.regs.pc, BASE); }
}

static void test_ldst() {
  constexpr uint64_t D = 0x8000;
  { Cpu c; c.regs.x[1] = D; c.regs.x[0] = 0x1122334455667788ull;
    run(c, {e::ldst_uimm(3, 0, 1, 1, 0),   // STR x0,[x1,#8]
            e::ldst_uimm(3, 1, 1, 1, 2),   // LDR x2,[x1,#8]
            e::ldst_uimm(2, 1, 2, 1, 3),   // LDR w3,[x1,#8]
            e::ldst_uimm(0, 1, 8, 1, 4),   // LDRB w4,[x1,#8]
            e::ldst_uimm(1, 1, 4, 1, 5),   // LDRH w5,[x1,#8]
            e::ldst_uimm(0, 2, 8, 1, 6),   // LDRSB x6,[x1,#8]  (0x88 negative)
            e::ldst_uimm(1, 3, 4, 1, 7),   // LDRSH w7,[x1,#8]  (0x7788 positive)
            e::ldst_uimm(2, 2, 3, 1, 8)}); // LDRSW x8,[x1,#12] (0x11223344 positive)
    CHECK_EQ(c.mem.read<uint64_t>(D + 8), 0x1122334455667788ull);
    CHECK_EQ(c.regs.x[2], 0x1122334455667788ull); CHECK_EQ(c.regs.x[3], 0x55667788ull);
    CHECK_EQ(c.regs.x[4], 0x88u); CHECK_EQ(c.regs.x[5], 0x7788u);
    CHECK_EQ(c.regs.x[6], 0xFFFFFFFFFFFFFF88ull); CHECK_EQ(c.regs.x[7], 0x7788u);
    CHECK_EQ(c.regs.x[8], 0x11223344ull); }
  { Cpu c; c.regs.x[1] = D; c.mem.write<uint32_t>(D, 0x80000000u);
    run(c, {e::ldst_uimm(2, 2, 0, 1, 2), e::ldst_uimm(2, 1, 0, 1, 3)});  // LDRSW x2 / LDR w3
    CHECK_EQ(c.regs.x[2], 0xFFFFFFFF80000000ull); CHECK_EQ(c.regs.x[3], 0x80000000ull); }
  { Cpu c; c.regs.x[1] = D; c.regs.x[0] = 0xAB;                             // pre/post/unscaled
    run(c, {e::ldst_imm9(3, 0, 16, 3, 1, 0),      // STR x0,[x1,#16]!
            e::ldst_imm9(3, 1, -8, 1, 1, 2),      // LDR x2,[x1],#-8   (post: reads [x1], then x1 -= 8)
            e::ldst_imm9(3, 0, -3, 0, 1, 0)});    // STUR x0,[x1,#-3]
    CHECK_EQ(c.regs.x[2], 0xABu); CHECK_EQ(c.regs.x[1], D + 8);
    CHECK_EQ(c.mem.read<uint64_t>(D + 5), 0xABu); }
  { Cpu c; c.regs.sp = 0x9000; c.regs.x[29] = 0xAAAA; c.regs.x[30] = 0xBBBB;
    run(c, {0xA9BF7BFDu,                                   // STP x29,x30,[sp,#-16]!
            e::movz(1, 0, 0, 29), e::movz(1, 0, 0, 30),
            0xA8C17BFDu});                                 // LDP x29,x30,[sp],#16
    CHECK_EQ(c.mem.read<uint64_t>(0x8FF0), 0xAAAAu); CHECK_EQ(c.mem.read<uint64_t>(0x8FF8), 0xBBBBu);
    CHECK_EQ(c.regs.x[29], 0xAAAAu); CHECK_EQ(c.regs.x[30], 0xBBBBu); CHECK_EQ(c.regs.sp, 0x9000u); }
  { Cpu c; c.regs.x[1] = D; c.regs.x[2] = 0x11111111; c.regs.x[3] = 0x22222222;
    run(c, {e::ldp_stp(0, 0, 2, 1, 3, 1, 2), e::ldp_stp(0, 1, 2, 1, 5, 1, 4)});  // STP w2,w3,[x1,#4]; LDP w4,w5,[x1,#4]
    CHECK_EQ(c.regs.x[4], 0x11111111u); CHECK_EQ(c.regs.x[5], 0x22222222u);
    CHECK_EQ(c.mem.read<uint32_t>(D + 4), 0x11111111u); }
  { Cpu c; c.regs.x[1] = 0x10000000; run(c, {e::ldst_uimm(3, 1, 0, 1, 0)});     // out-of-range load faults
    CHECK(c.halt == HaltReason::MemFault); CHECK_EQ(c.regs.pc, BASE); }
  { Cpu c; c.regs.x[1] = D; run(c, {e::ldst_uimm(3, 1, 0, 1, 31)}); CHECK_EQ(c.regs.x[31], 0u); }  // Rt=31 is XZR
}

static void test_system() {
  { Cpu c; c.regs.x[1] = 0xCAFE; run(c, {e::msr(e::FIELD_TPIDR_EL0, 1), e::mrs(e::FIELD_TPIDR_EL0, 0)}); CHECK_EQ(c.regs.x[0], 0xCAFEu); }
  { Cpu c; c.regs.x[1] = 0xA0000000ull; run(c, {e::msr(e::FIELD_NZCV, 1), e::mrs(e::FIELD_NZCV, 0)});
    FLAGS(c, 1, 0, 1, 0); CHECK_EQ(c.regs.x[0], 0xA0000000ull); }
  { Cpu c; run(c, {e::mrs(0x1234, 0)}); CHECK(c.halt == HaltReason::Invalid); }  // unmodelled sysreg is rejected, not invented
  { Cpu c; run(c, {e::nop(), 0xD5033FDFu /*ISB*/, 0xD5033BBFu /*DMB ISH*/, 0xD5033B9Fu /*DSB ISH*/}); CHECK_EQ(c.steps, 5u); CHECK(c.halt == HaltReason::ZeroWord); }
}

int main() {
  test_known_encodings();
  test_dp_imm();
  test_flags();
  test_dp_reg();
  test_branches();
  test_ldst();
  test_system();
  std::printf("%d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
