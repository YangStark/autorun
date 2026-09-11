#!/usr/bin/env python3
"""Check the Horizon server's hardware mouse message filter against Wine's rules."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
horizon = (root / 'dlls/ntdll/unix/horizon.c').read_text()

def function(source, name):
    start = source.index(name)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

defines = '\n'.join(re.findall(r'^#define HORIZON_WM_\w+ .*$', horizon, re.M))
fixture = r'''
#include <assert.h>
#include <stdio.h>
struct horizon_get_message_request { unsigned int get_first, get_last; };
struct horizon_input_message { unsigned int msg; };
'''
tests = r'''
static int matches(unsigned int queued, unsigned int first, unsigned int last) {
    struct horizon_get_message_request request = {first, last};
    struct horizon_input_message message = {queued};
    return horizon_server_mouse_message_matches(&request, &message);
}
int main(void) {
    enum { MOVE = 0x200, LDOWN = 0x201, LUP = 0x202, LDBL = 0x203, RDOWN = 0x204, RUP = 0x205, RDBL = 0x206,
           NCMOVE = 0xa0, NCLDOWN = 0xa1, NCLDBL = 0xa3, NCRDOWN = 0xa4, NCRUP = 0xa5, NCRDBL = 0xa6 };
    assert(matches(RDOWN, 0, 0) && matches(MOVE, 0, 0xffffffff));
    assert(matches(RDOWN, RDOWN, RDOWN) && matches(RDOWN, 0x200, 0x209));
    /* Hit testing can make any of them a non-client message. */
    assert(matches(RDOWN, NCRDOWN, NCRDOWN) && matches(MOVE, NCMOVE, NCMOVE) && matches(RUP, NCRUP, NCRUP));
    /* A second click in time becomes a double-click, which menu tracking then
     * removes with exactly that message as the filter. */
    assert(matches(RDOWN, RDBL, RDBL) && matches(RDOWN, NCRDBL, NCRDBL));
    assert(matches(LDOWN, LDBL, LDBL) && matches(LDOWN, NCLDBL, NCLDBL) && matches(LDOWN, NCLDOWN, NCLDOWN));
    /* Only presses turn into double-clicks. */
    assert(!matches(RUP, 0x207, 0x207) && !matches(LUP, RDOWN, RDOWN) && !matches(MOVE, LUP, LUP));
    assert(!matches(RDOWN, LDOWN, LUP) && !matches(LDOWN, RDOWN, RDBL) && !matches(RDOWN, 0x100, 0x108));
    puts("PASS: mouse filter matches moves, clicks, non-client and double-click forms like Wine's server");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='wine-nx-mouse-filter-') as temp:
    src = Path(temp) / 'test.c'
    exe = Path(temp) / 'test'
    src.write_text(fixture + defines + '\n' +
                   function(horizon, 'static int horizon_server_msg_in_filter(') + '\n' +
                   function(horizon, 'static int horizon_server_mouse_message_matches(') + tests)
    subprocess.run(['cc', '-g', '-Wall', '-Werror', '-fsanitize=address,undefined', str(src), '-o', str(exe)],
                   check=True)
    subprocess.run([str(exe)], check=True)
