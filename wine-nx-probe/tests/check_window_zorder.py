#!/usr/bin/env python3
"""Run the Horizon server's window z-order (horizon_server_link_window_locked) on a game's windows."""
from pathlib import Path
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

defines = '\n'.join(line for line in horizon.splitlines()
                    if line.startswith(('#define HORIZON_WS_EX_TOPMOST', '#define HORIZON_LINK_')))
fixture = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
struct horizon_user_window { unsigned int handle, parent, owner, ex_style; struct horizon_user_window *next; };
static struct horizon_user_window *horizon_windows;
static struct horizon_user_window windows[32];
static unsigned int window_count;
static struct horizon_user_window *horizon_server_find_window_locked(unsigned int handle)
{
    for (unsigned int i = 0; i < window_count; i++) if (windows[i].handle == handle) return &windows[i];
    return NULL;
}
'''
tests = r'''
enum { DESKTOP = 0x10020, TOP = 0, BOTTOM = 1, TOPMOST = -1, NOTOPMOST = -2 };

/* horizon_server_create_window_locked() puts new windows at the head. */
static unsigned int create(unsigned int parent, unsigned int owner)
{
    struct horizon_user_window *window = &windows[window_count++];
    window->handle = DESKTOP + 2 * (window_count - 1);
    window->parent = parent;
    window->owner = owner;
    window->next = horizon_windows;
    horizon_windows = window;
    return window->handle;
}

/* The children of parent, top first, as creation indices. */
static const char *order(unsigned int parent)
{
    static char text[64];
    unsigned int len = 0;
    for (struct horizon_user_window *window = horizon_windows; window; window = window->next)
        if (window->parent == parent)
            len += snprintf(text + len, sizeof(text) - len, "%s%u", len ? " " : "", (window->handle - DESKTOP) / 2);
    text[len] = 0;
    return text;
}

static void zlink(unsigned int handle, int previous)
{
    horizon_server_link_window_locked(horizon_server_find_window_locked(handle), (unsigned int)previous);
}

static int topmost(unsigned int handle)
{
    return !!(horizon_server_find_window_locked(handle)->ex_style & HORIZON_WS_EX_TOPMOST);
}

static unsigned int total(void)
{
    unsigned int count = 0;
    for (struct horizon_user_window *window = horizon_windows; window; window = window->next) count++;
    return count;
}

int main(void)
{
    assert(create(0, 0) == DESKTOP);
    unsigned int game = create(DESKTOP, 0);   /* 1: WarCraft III's window, created first */
    create(game, 0);                          /* 2 and 3: its children */
    create(game, 0);
    unsigned int helper = create(DESKTOP, 0); /* 4: a later top-level window over it */
    assert(!strcmp(order(DESKTOP), "4 1"));

    /* SetWindowPos(game, HWND_TOP) raises the game's window over the helper. */
    zlink(game, TOP);
    assert(!strcmp(order(DESKTOP), "1 4") && !strcmp(order(game), "3 2") && total() == 5);
    zlink(game, TOP);
    assert(!strcmp(order(DESKTOP), "1 4"));

    /* HWND_TOPMOST goes above everything; HWND_TOP stays below topmost windows. */
    zlink(helper, TOPMOST);
    assert(!strcmp(order(DESKTOP), "4 1") && topmost(helper));
    zlink(game, TOP);
    assert(!strcmp(order(DESKTOP), "4 1") && !topmost(game));

    /* A popup owned by a topmost window, raised, goes above its owner and is topmost too. */
    unsigned int popup = create(DESKTOP, helper);  /* 5 */
    zlink(popup, BOTTOM);
    assert(!strcmp(order(DESKTOP), "4 1 5") && !topmost(popup));
    zlink(popup, TOP);
    assert(!strcmp(order(DESKTOP), "5 4 1") && topmost(popup));

    /* HWND_NOTOPMOST drops the flag and keeps it above the windows that are not topmost. */
    zlink(helper, NOTOPMOST);
    assert(!strcmp(order(DESKTOP), "5 4 1") && !topmost(helper));
    zlink(helper, NOTOPMOST);  /* no longer topmost: nothing to do */
    assert(!strcmp(order(DESKTOP), "5 4 1"));

    /* HWND_BOTTOM puts it last and drops the flag. */
    zlink(popup, BOTTOM);
    assert(!strcmp(order(DESKTOP), "4 1 5") && !topmost(popup));

    /* After a sibling: right below it, topmost only between topmost windows. */
    zlink(helper, (int)game);
    assert(!strcmp(order(DESKTOP), "1 4 5") && !topmost(helper));
    zlink(game, TOPMOST);
    zlink(popup, TOPMOST);
    assert(!strcmp(order(DESKTOP), "5 1 4"));
    zlink(helper, (int)popup);
    assert(!strcmp(order(DESKTOP), "5 4 1") && topmost(helper));

    /* The children kept their order, and no window was lost. */
    assert(!strcmp(order(game), "3 2") && total() == 6);
    puts("PASS: HWND_TOP, HWND_BOTTOM, HWND_TOPMOST, HWND_NOTOPMOST, after a sibling, owned popups over topmost "
         "owners, children untouched");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='wine-nx-zorder-') as temp:
    src = Path(temp) / 'test.c'
    exe = Path(temp) / 'test'
    src.write_text(defines + '\n' + fixture + function(horizon, 'static void horizon_server_link_window_locked(') + tests)
    subprocess.run(['cc', '-g', '-Wall', '-Werror', '-Wno-unused-function', '-fsanitize=address,undefined',
                    str(src), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
