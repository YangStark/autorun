#!/usr/bin/env python3
"""Check the production positioned-I/O shims, including OpenTTD's header probe."""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
path = 'wine-nx-probe/source/runtime_platform.c'
source = (root / path).read_text()
if '--baseline' in sys.argv:
    source = subprocess.check_output(['git', '-C', str(root), 'show', f'HEAD:{path}'], text=True)
start = source.index('ssize_t pread(')
code = source[start:source.index('int openat(', start)]
code = code.replace('pread(', 'nx_pread(').replace('pwrite(', 'nx_pwrite(')
prefix = r'''
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static pthread_mutex_t positioned_io_mutex = PTHREAD_MUTEX_INITIALIZER;
'''
test = r'''
int main(void) {
    char name[] = "/tmp/wine-nx-position-XXXXXX", data[128] = "LANG\xab\x09\xd1\x2a", buf[32];
    int fd = mkstemp(name);
    assert(fd >= 0 && write(fd, data, sizeof(data)) == sizeof(data));
    assert(lseek(fd, 0, SEEK_SET) == 0);
    assert(nx_pread(fd, buf, 23, 0) == 23); /* Wine device-placeholder probe */
    assert(read(fd, buf, 8) == 8);
    assert(!memcmp(buf, data, 8)); /* OpenTTD must receive LANG + version. */
    assert(lseek(fd, 37, SEEK_SET) == 37);
    assert(nx_pread(fd, buf, 32, 120) == 8);
    assert(lseek(fd, 0, SEEK_CUR) == 37);
    assert(nx_pread(fd, buf, 32, 200) == 0);
    assert(lseek(fd, 0, SEEK_CUR) == 37);
    assert(nx_pread(fd, buf, 0, 0) == 0);
    assert(lseek(fd, 0, SEEK_CUR) == 37);
    assert(nx_pread(fd, buf, 8, -1) == -1 && errno == EINVAL);
    assert(lseek(fd, 0, SEEK_CUR) == 37);
    assert(nx_pwrite(fd, "test", 4, 8) == 4);
    assert(lseek(fd, 0, SEEK_CUR) == 37);
    assert(nx_pread(fd, buf, 4, 8) == 4 && !memcmp(buf, "test", 4));
    int ro = open(name, O_RDONLY);
    assert(ro >= 0 && lseek(ro, 19, SEEK_SET) == 19);
    assert(nx_pwrite(ro, "x", 1, 0) == -1 && errno == EBADF);
    assert(lseek(ro, 0, SEEK_CUR) == 19);
    close(ro); close(fd); unlink(name);
    puts("PASS: language header after probe, nonzero cursor, EOF, short/zero reads, positioned write, failures");
}
'''
with tempfile.TemporaryDirectory(prefix='wine-nx-io-') as temp:
    src, exe = Path(temp) / 'test.c', Path(temp) / 'test'
    src.write_text(prefix + code + test)
    subprocess.run(['cc', '-fsanitize=address,undefined', '-pthread', str(src), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
