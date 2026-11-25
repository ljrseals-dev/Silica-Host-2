/* SilicaHost <-> wasm core ABI (version 1). Mirrored in src/abi.ts; keep in sync.
 * All values little-endian. u64 parameters/returns appear as BigInt in JS. */
#pragma once
#define SILICA_ABI_VERSION 1u

/* Layout of the state snapshot: SILICA_STATE_WORDS u64 words at silica_state_ptr(). */
#define SILICA_ST_X0        0   /* X0..X30 -> 0..30 */
#define SILICA_ST_SP        31
#define SILICA_ST_PC        32
#define SILICA_ST_NZCV      33
#define SILICA_ST_EL        34
#define SILICA_ST_SPSEL     35
#define SILICA_ST_STEPS     36
#define SILICA_ST_HALT      37  /* core HaltReason: 0 none,1 zero word,2 BRK,3 invalid,4 mem fault */
#define SILICA_ST_EXITED    38  /* 1 once the guest called hostcall exit */
#define SILICA_ST_EXITCODE  39
#define SILICA_ST_SYS0      40  /* TPIDR_EL0, VBAR_EL1, ELR_EL1, SPSR_EL1 -> 40..43 */
#define SILICA_ST_V0        48  /* V0..V31 as (lo,hi) pairs -> 48..111 */
#define SILICA_STATE_WORDS  112

/* silica_run() stop reasons */
#define SILICA_STOP_BUDGET      0
#define SILICA_STOP_HALT        1
#define SILICA_STOP_BREAKPOINT  2
#define SILICA_STOP_CONSOLE     3  /* console buffer is nearly full: drain it, then run again */
#define SILICA_STOP_EXIT        4

/* Hostcall: guest executes BRK #SILICA_HOSTCALL_IMM; x0 = op, x1..x2 = args, result in x0.
 *   0 exit(x1)            -> stops with SILICA_STOP_EXIT
 *   1 putc(x1 & 0xff)     -> x0 = 1 (or 0 if buffer full)
 *   2 write(ptr=x1,len=x2)-> x0 = bytes accepted (may be < len if the buffer is full)
 *   other                 -> x0 = -1
 * This is a SilicaHost convention, not Arm semihosting. */
#define SILICA_HOSTCALL_IMM 0x5110u
#define SILICA_CONSOLE_CAP  4096u
#define SILICA_MAX_BREAKPOINTS 64u
