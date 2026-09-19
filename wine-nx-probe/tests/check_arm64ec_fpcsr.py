#!/usr/bin/env python3
"""Check CPU64 floating-point control translation against the Wine ARM64EC ABI."""
from pathlib import Path
import os
import subprocess
import tempfile


root = Path(__file__).resolve().parents[2]
cpu = (root / 'dlls/winebox64ec/cpu.c').read_text()
unwind = (root / 'dlls/ntdll/unwind.h').read_text()


def function(source, name):
    start = source.rfind('\n', 0, source.index(name + '(')) + 1
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


fixture = '''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
typedef uint32_t ULONG, UINT;
typedef uint64_t ULONGLONG, UINT64;
'''
for name in ('mxcsr_to_fpcsr', 'fpcsr_to_mxcsr'):
    fixture += function(cpu, name)
    fixture += function(unwind, name).replace(name, 'wine_' + name)
fixture += r'''
int main(void)
{
    unsigned int mxcsr;
    const uint32_t rounding[] = {0, 0x800000, 0x400000, 0xc00000};

    for (mxcsr = 0; mxcsr < 4; ++mxcsr)
    {
        assert(mxcsr_to_fpcsr(0x1f80 | (mxcsr << 13)) == rounding[mxcsr]);
        assert(fpcsr_to_mxcsr(rounding[mxcsr], 0) == (0x1f80 | (mxcsr << 13)));
    }
    for (mxcsr = 0; mxcsr <= 0xffff; ++mxcsr)
    {
        uint64_t native = mxcsr_to_fpcsr(mxcsr);
        uint64_t expected = wine_mxcsr_to_fpcsr(mxcsr);

        assert(native == expected);
        assert(fpcsr_to_mxcsr(native, native >> 32) == mxcsr);
        assert(fpcsr_to_mxcsr(expected, expected >> 32) ==
               wine_fpcsr_to_mxcsr(expected, expected >> 32));
    }
    puts("PASS: CPU64 FPCR/FPSR conversion matches Wine for all 65536 MXCSR values");
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='wine-nx-arm64ec-fpcsr-') as temp:
    source, binary = Path(temp) / 'fpcsr.c', Path(temp) / 'fpcsr'
    source.write_text(fixture)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
