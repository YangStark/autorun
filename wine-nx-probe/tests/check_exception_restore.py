#!/usr/bin/env python3
"""Run the exception-context restores in dlls/ntdll/unix/horizon.c on an AArch64 Mac.

A handled fault resumes through a restore that loads every register from the
libnx dump and jumps to its pc, and that jump needs one register.
horizon_restore_exception_context gives up x17. That was build 62's freeze:
Box64 keeps the guest's EDI in x17 (vendor/box64/src/dynarec/arm64/
arm64_mapping.h), so a fault handled in translated code resumed with EDI holding
the fault address. horizon_restore_exception_context_x9 gives up x9, which Box64
never uses.

Each restore runs for real and jumps into a routine that records the registers;
they are compared with the dump. x18 is not compared: macOS reserves it."""
from pathlib import Path
import platform
import re
import subprocess
import sys
import tempfile

if platform.system() != 'Darwin' or platform.machine() != 'arm64':
    print('exception restore: skipped (needs an AArch64 Mac)')
    sys.exit(0)

root = Path(__file__).resolve().parents[2]
source = (root / 'dlls/ntdll/unix/horizon.c').read_text()


def restore_asm(name):
    start = source.index(f'static void {name}( ThreadExceptionDump *ctx )\n{{')
    body = source[start:source.index('\n}\n', start)]
    asm = body[body.index('__asm__ __volatile__('):]
    asm = asm[:asm.index('\n        :\n')]
    lines = re.findall(r'"((?:[^"\\]|\\.)*)\\n"', asm)
    assert lines and lines[0] == 'mov x21, %0', lines[:1]
    return ''.join(line.replace('%0', 'x0') + '\\n' for line in lines)


capture = ['stp x0, x1, [sp, #-16]!', 'adrp x0, _captured@PAGE', 'add x0, x0, _captured@PAGEOFF',
           'ldr x1, [sp]', 'str x1, [x0]', 'ldr x1, [sp, #8]', 'str x1, [x0, #8]']
capture += [f'stp x{r}, x{r + 1}, [x0, #{8 * r}]' for r in range(2, 30, 2)]
capture += ['str x30, [x0, #240]', 'add x1, sp, #16', 'str x1, [x0, #248]', 'mrs x1, nzcv', 'str x1, [x0, #256]']
capture += [f'stp q{q}, q{q + 1}, [x0, #{272 + 16 * q}]' for q in range(0, 32, 2)]
capture += ['add sp, sp, #16', 'adrp x0, _back@PAGE', 'add x0, x0, _back@PAGEOFF', 'mov w1, #1', 'b _longjmp']

program = r'''
#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct
{
    uint32_t error_desc, pad[3];
    uint64_t cpu_gprs[29], fp, lr, sp, pc, padding;
    unsigned __int128 fpu_gprs[32];
    uint32_t pstate, afsr0, afsr1, esr;
    uint64_t far;
} ThreadExceptionDump;
_Static_assert( offsetof(ThreadExceptionDump, cpu_gprs) == 16, "cpu_gprs" );
_Static_assert( offsetof(ThreadExceptionDump, fp) == 248, "fp" );
_Static_assert( offsetof(ThreadExceptionDump, pc) == 272, "pc" );
_Static_assert( offsetof(ThreadExceptionDump, fpu_gprs) == 288, "fpu_gprs" );
_Static_assert( offsetof(ThreadExceptionDump, pstate) == 800, "pstate" );

uint64_t captured[128];  /* x0-x30, sp, nzcv, then q0-q31 from 272 */
jmp_buf back;
static unsigned char stack[1 << 16];
static unsigned int failures;

void restore_x17( ThreadExceptionDump *ctx ) __attribute__((noreturn));
void restore_x9( ThreadExceptionDump *ctx ) __attribute__((noreturn));
void capture( void );

__asm__( ".text\n.p2align 2\n"
         ".globl _restore_x17\n_restore_x17:\n" RESTORE_X17
         ".globl _restore_x9\n_restore_x9:\n" RESTORE_X9
         ".globl _capture\n_capture:\n" CAPTURE );

static void run( const char *name, void (*restore)( ThreadExceptionDump * ), unsigned int given_up )
{
    ThreadExceptionDump dump;
    unsigned __int128 q;
    unsigned int i;

    memset( &dump, 0, sizeof(dump) );
    memset( captured, 0, sizeof(captured) );
    for (i = 0; i < 29; i++) dump.cpu_gprs[i] = 0x1000000000000000ull | (uint64_t)i << 8 | i;
    dump.fp = 0x2900000000000029ull;
    dump.lr = 0x3000000000000030ull;
    dump.sp = ((uintptr_t)stack + sizeof(stack)) & ~(uintptr_t)15;
    dump.pc = (uintptr_t)capture;
    dump.pstate = 0x60000000;  /* Z and C */
    for (i = 0; i < 32; i++) dump.fpu_gprs[i] = (unsigned __int128)(0x5100u + i) << 64 | (0x7700u + i);

    if (!setjmp( back )) restore( &dump );

    for (i = 0; i < 29; i++)
    {
        uint64_t want = i == given_up ? dump.pc : dump.cpu_gprs[i];

        if (i == 18 || captured[i] == want) continue;
        printf( "%s: x%u = %016llx, want %016llx\n", name, i, (unsigned long long)captured[i], (unsigned long long)want );
        failures++;
    }
    if (captured[29] != dump.fp || captured[30] != dump.lr || captured[31] != dump.sp)
    {
        printf( "%s: fp, lr or sp differ\n", name );
        failures++;
    }
    if ((captured[32] & 0xf0000000) != dump.pstate)
    {
        printf( "%s: nzcv %llx\n", name, (unsigned long long)captured[32] );
        failures++;
    }
    for (i = 0; i < 32; i++)
    {
        memcpy( &q, (unsigned char *)captured + 272 + 16 * i, sizeof(q) );
        if (q == dump.fpu_gprs[i]) continue;
        printf( "%s: q%u differs\n", name, i );
        failures++;
    }
}

int main( void )
{
    run( "x17 restore", restore_x17, 17 );
    run( "x9 restore", restore_x9, 9 );
    if (failures) return 1;
    puts( "exception restore: every register, sp, flags and SIMD come back; x17 gives up EDI, x9 keeps it" );
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='wine-nx-restore-') as tmp:
    c = Path(tmp) / 'restore.c'
    exe = Path(tmp) / 'restore'
    c.write_text(program)
    defines = [f'-DRESTORE_X17="{restore_asm("horizon_restore_exception_context")}"',
               f'-DRESTORE_X9="{restore_asm("horizon_restore_exception_context_x9")}"',
               '-DCAPTURE="' + ''.join(line + '\\n' for line in capture) + '"']
    subprocess.run(['clang', '-std=gnu11', '-Wall', '-Wextra', '-Werror', *defines, str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
