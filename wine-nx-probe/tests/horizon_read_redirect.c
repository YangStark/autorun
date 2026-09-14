/* Host test for dlls/ntdll/unix/horizon_read_redirect.h, on an AArch64 Mac.
 *
 * Each snippet loads relative to a base in x1 (index in x2) and stores what it
 * loaded. It runs against a readable page, then against an inaccessible one
 * whose faults the header emulates from a copy of that page: the results,
 * writeback included, must match what the processor itself did. */
#if !defined(__aarch64__) || !defined(__APPLE__)
#include <stdio.h>

int main(void)
{
    puts( "horizon_read_redirect: skipped (needs an AArch64 Mac)" );
    return 0;
}
#else
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "../../dlls/ntdll/unix/horizon_read_redirect.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define WINDOW 0x1000
#define MAPPING 0x4000  /* the Mac's page size */

/* x3/x4 and q1/q2 start all ones, so a load that fails to clear the upper bits
 * of its register shows up. out[] = x3, x4, q1, q2, x10, x1. */
#define SNIPPET(name, body) \
    __asm__( ".text\n.p2align 2\n.globl _" #name "\n_" #name ":\n" \
             "mov x3, #-1\nmov x4, #-1\nmovi v1.2d, #0xffffffffffffffff\n" \
             "movi v2.2d, #0xffffffffffffffff\nmov x10, #0\n" \
             body \
             "stp x3, x4, [x0]\nstp q1, q2, [x0, #16]\nstp x10, x1, [x0, #48]\nret\n" ); \
    extern void name( unsigned long long *out, unsigned long long base, unsigned long long index );

SNIPPET(ldr_x_u12,      "ldr x3, [x1, #0x320]\n")
SNIPPET(ldr_x_u12_end,  "ldr x3, [x1, #0xff8]\n")
SNIPPET(ldr_w_u12,      "ldr w3, [x1, #0x324]\n")
SNIPPET(ldrb_u12,       "ldrb w3, [x1, #0x2d4]\n")
SNIPPET(ldrh_u12,       "ldrh w3, [x1, #0x26a]\n")
SNIPPET(ldrsw_u12,      "ldrsw x3, [x1, #0x2d8]\n")
SNIPPET(ldrsb_w_u12,    "ldrsb w3, [x1, #0x2d5]\n")
SNIPPET(ldrsb_x_u12,    "ldrsb x3, [x1, #0x2d5]\n")
SNIPPET(ldrsh_w_u12,    "ldrsh w3, [x1, #0x26a]\n")
SNIPPET(ldrsh_x_u12,    "ldrsh x3, [x1, #0x26a]\n")
SNIPPET(ldur_x,         "add x1, x1, #0x400\nldur x3, [x1, #-0x100]\n")
SNIPPET(ldursw,         "add x1, x1, #0x400\nldursw x3, [x1, #-0xff]\n")
SNIPPET(ldr_x_pre,      "ldr x3, [x1, #0x40]!\n")
SNIPPET(ldr_w_post,     "ldr w3, [x1], #0x20\n")
SNIPPET(ldrb_post_neg,  "add x1, x1, #0x10\nldrb w3, [x1], #-0x10\n")
SNIPPET(ldr_x_reg_lsl,  "ldr x3, [x1, x2, lsl #3]\n")
SNIPPET(ldr_w_reg_uxtw, "ldr w3, [x1, w2, uxtw #2]\n")
SNIPPET(ldr_x_reg_sxtw, "add x1, x1, #0x100\nldr x3, [x1, w2, sxtw]\n")
SNIPPET(ldrb_reg,       "ldrb w3, [x1, x2]\n")
SNIPPET(ldrsh_reg,      "ldrsh x3, [x1, x2, lsl #1]\n")
SNIPPET(ldr_d_u12,      "ldr d1, [x1, #0x3c8]\n")
SNIPPET(ldr_q_u12,      "ldr q1, [x1, #0x3d0]\n")
SNIPPET(ldr_s_u12,      "ldr s1, [x1, #0x3d8]\n")
SNIPPET(ldr_b_h_u12,    "ldr b1, [x1, #0x3d9]\nldr h2, [x1, #0x3da]\n")
SNIPPET(ldur_q,         "ldur q1, [x1, #0x13]\n")
SNIPPET(ldr_q_reg,      "ldr q1, [x1, x2, lsl #4]\n")
SNIPPET(ldr_d_pre,      "ldr d1, [x1, #8]!\n")
SNIPPET(ldr_q_post,     "ldr q1, [x1], #0x30\n")
SNIPPET(ldp_x,          "ldp x3, x4, [x1, #0x10]\n")
SNIPPET(ldp_w_pre,      "ldp w3, w4, [x1, #0x20]!\n")
SNIPPET(ldpsw_post,     "ldpsw x3, x4, [x1], #0x18\n")
SNIPPET(ldp_d,          "ldp d1, d2, [x1, #0x40]\n")
SNIPPET(ldp_q,          "ldp q1, q2, [x1, #0x80]\n")
SNIPPET(ldp_s_pre,      "ldp s1, s2, [x1, #0x30]!\n")
/* x10 = how far the pre-index moved sp */
SNIPPET(ldr_sp,         "mov x9, sp\nmov sp, x1\nldr x3, [sp, #0x28]\nldr x4, [sp, #16]!\n"
                        "mov x10, sp\nsub x10, x10, x1\nmov sp, x9\n")

/* Instructions the header must refuse, and the base each one runs with. */
__asm__( ".text\n.p2align 2\n.globl _refused\n_refused:\n"
         "str x3, [x1, #8]\n"
         "stp x3, x4, [x1]\n"
         "ldr x3, [x1, #0x1000]\n"   /* past the window */
         "ldur x3, [x1, #-8]\n"      /* before it */
         "ldr x3, [x1]\n"            /* base at 0xffc: 8 bytes cross the end */
         ".inst 0xf8408c21\n"        /* ldr x1, [x1, #8]! - writeback into the target */
         ".inst 0xa9400c23\n"        /* ldp x3, x3, [x1] */
         "prfm pldl1keep, [x1, #8]\n"
         "ldr x3, .\n"               /* literal */
         "add x3, x3, #1\n" );
extern const unsigned int refused[];
static const unsigned long long refused_base[] = { 0, 0, 0, 0, 0xffc, 0, 0, 0, 0, 0 };

static const struct
{
    const char *name;
    void (*fn)( unsigned long long *, unsigned long long, unsigned long long );
    unsigned long long index;
}
#define TEST(name, index) { #name, name, index }
tests[] =
{
    TEST(ldr_x_u12, 0), TEST(ldr_x_u12_end, 0), TEST(ldr_w_u12, 0),
    TEST(ldrb_u12, 0), TEST(ldrh_u12, 0), TEST(ldrsw_u12, 0),
    TEST(ldrsb_w_u12, 0), TEST(ldrsb_x_u12, 0), TEST(ldrsh_w_u12, 0), TEST(ldrsh_x_u12, 0),
    TEST(ldur_x, 0), TEST(ldursw, 0),
    TEST(ldr_x_pre, 0), TEST(ldr_w_post, 0), TEST(ldrb_post_neg, 0),
    TEST(ldr_x_reg_lsl, 0x60),
    TEST(ldr_w_reg_uxtw, 0xabcdef0000000080ull),   /* only w2 counts */
    TEST(ldr_x_reg_sxtw, 0x12345678fffffff0ull),   /* w2 = -16 */
    TEST(ldrb_reg, 0x2d4), TEST(ldrsh_reg, 0x135),
    TEST(ldr_d_u12, 0), TEST(ldr_q_u12, 0), TEST(ldr_s_u12, 0), TEST(ldr_b_h_u12, 0),
    TEST(ldur_q, 0), TEST(ldr_q_reg, 0x10), TEST(ldr_d_pre, 0), TEST(ldr_q_post, 0),
    TEST(ldp_x, 0), TEST(ldp_w_pre, 0), TEST(ldpsw_post, 0),
    TEST(ldp_d, 0), TEST(ldp_q, 0), TEST(ldp_s_pre, 0),
    TEST(ldr_sp, 0),
};

static unsigned long long window;
static unsigned char *backing;
static unsigned int emulated;

static void handler( int sig, siginfo_t *info, void *context )
{
    static const char msg[] = "horizon_read_redirect: fault the header did not emulate\n";
    ucontext_t *uc = context;
    struct horizon_read_regs regs;
    unsigned int i, insn;

    (void)sig;
    for (i = 0; i < 29; i++) regs.x[i] = uc->uc_mcontext->__ss.__x[i];
    regs.x[29] = uc->uc_mcontext->__ss.__fp;
    regs.x[30] = uc->uc_mcontext->__ss.__lr;
    regs.sp = uc->uc_mcontext->__ss.__sp;
    regs.pc = uc->uc_mcontext->__ss.__pc;
    for (i = 0; i < 32; i++) regs.v[i] = uc->uc_mcontext->__ns.__v[i];
    memcpy( &insn, (void *)regs.pc, sizeof(insn) );
    if ((unsigned long long)info->si_addr - window >= WINDOW ||
        !horizon_redirect_read( &regs, insn, window, backing, WINDOW ))
    {
        (void)!write( 2, msg, sizeof(msg) - 1 );
        _exit( 1 );
    }
    for (i = 0; i < 29; i++) uc->uc_mcontext->__ss.__x[i] = regs.x[i];
    uc->uc_mcontext->__ss.__fp = regs.x[29];
    uc->uc_mcontext->__ss.__lr = regs.x[30];
    uc->uc_mcontext->__ss.__sp = regs.sp;
    uc->uc_mcontext->__ss.__pc = regs.pc;
    for (i = 0; i < 32; i++) uc->uc_mcontext->__ns.__v[i] = regs.v[i];
    emulated++;
}

int main(void)
{
    static unsigned char stack[1 << 16];
    stack_t ss = { .ss_sp = stack, .ss_size = sizeof(stack) };
    struct sigaction sa = { 0 };
    unsigned char *reference;
    unsigned int i, failures = 0;

    reference = mmap( NULL, MAPPING, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0 );
    backing = malloc( WINDOW );
    window = (unsigned long long)mmap( NULL, MAPPING, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0 );
    if (reference == MAP_FAILED || !backing || window == (unsigned long long)MAP_FAILED) return 1;
    for (i = 0; i < MAPPING; i++) reference[i] = (unsigned char)(i * 7 + 0x81);
    memcpy( backing, reference, WINDOW );

    if (sigaltstack( &ss, NULL )) return 1;
    sa.sa_sigaction = handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigaction( SIGSEGV, &sa, NULL );
    sigaction( SIGBUS, &sa, NULL );

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        unsigned long long expect[8], got[8];
        unsigned int before = emulated;

        tests[i].fn( expect, (unsigned long long)reference, tests[i].index );
        tests[i].fn( got, window, tests[i].index );
        expect[7] -= (unsigned long long)reference;
        got[7] -= window;
        if (!memcmp( expect, got, sizeof(expect) ) && emulated > before) continue;
        printf( "FAIL %s (%u emulated):\n", tests[i].name, emulated - before );
        for (unsigned int j = 0; j < 8; j++) printf( "  [%u] cpu %016llx emulated %016llx\n", j, expect[j], got[j] );
        failures++;
    }

    for (i = 0; i < ARRAY_SIZE(refused_base); i++)
    {
        struct horizon_read_regs regs, copy;

        memset( &regs, 0x5a, sizeof(regs) );
        regs.x[1] = window + refused_base[i];
        regs.pc = 0x100000;
        copy = regs;
        if (!horizon_redirect_read( &regs, refused[i], window, backing, WINDOW ) &&
            !memcmp( &regs, &copy, sizeof(regs) ))
            continue;
        printf( "FAIL refused[%u] %08x was emulated or changed the registers\n", i, refused[i] );
        failures++;
    }

    if (failures) return 1;
    printf( "horizon_read_redirect: ok (%zu loads, %u faults emulated, %zu refusals)\n",
            ARRAY_SIZE(tests), emulated, ARRAY_SIZE(refused_base) );
    return 0;
}
#endif
