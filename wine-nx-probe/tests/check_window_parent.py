#!/usr/bin/env python3
"""Run the Horizon server's SetParent (horizon_server_set_parent_locked) and
GetAncestor parent list (horizon_server_window_parents_locked) on the windows of
WarCraft III's movies: the DirectShow video window put in the movie window."""
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
                    if line.startswith(('#define HORIZON_WS_EX_TOPMOST', '#define HORIZON_LINK_',
                                        '#define HORIZON_STATUS_SUCCESS', '#define HORIZON_STATUS_INVALID_PARAMETER')))
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
enum { DESKTOP = 0x10020 };

/* New windows go at the head, as horizon_server_create_window_locked() puts them. */
static unsigned int create(unsigned int handle, unsigned int parent, unsigned int ex_style)
{
    struct horizon_user_window *window = &windows[window_count++];

    window->handle = handle;
    window->parent = parent;
    window->ex_style = ex_style;
    window->next = horizon_windows;
    horizon_windows = window;
    return handle;
}

/* The children of parent, topmost first, as a string of handles. */
static const char *children(unsigned int parent)
{
    static char text[256];
    char *end = text;

    *text = 0;
    for (struct horizon_user_window *w = horizon_windows; w; w = w->next)
        if (w->parent == parent) end += sprintf(end, "%s%x", end == text ? "" : " ", w->handle);
    return text;
}

static void expect(const char *what, const char *got, const char *expected)
{
    if (strcmp(got, expected)) { printf("FAIL %s: %s, expected %s\n", what, got, expected); assert(0); }
}

int main(void)
{
    unsigned int handles[4], count;

    create(DESKTOP, 0, 0);
    create(0x100, DESKTOP, HORIZON_WS_EX_TOPMOST);   /* Warcraft III, the game window */
    create(0x200, DESKTOP, 0);                       /* ActiveMovie Window, made hidden and top-level */
    create(0x300, DESKTOP, HORIZON_WS_EX_TOPMOST);   /* BlizPlay, the movie window, made last */
    create(0x310, 0x300, 0);                         /* a child already in the movie window */
    expect("top-level windows", children(DESKTOP), "300 200 100");

    /* put_Owner: SetParent(video, movie window) */
    assert(horizon_server_set_parent_locked(horizon_server_find_window_locked(0x200),
                                            horizon_server_find_window_locked(0x300)) == HORIZON_STATUS_SUCCESS);
    expect("top-level windows after SetParent", children(DESKTOP), "300 100");
    expect("movie window children", children(0x300), "200 310");

    count = horizon_server_window_parents_locked(horizon_server_find_window_locked(0x200), handles, 4);
    assert(count == 2 && handles[0] == 0x300 && handles[1] == DESKTOP);
    count = horizon_server_window_parents_locked(horizon_server_find_window_locked(0x200), handles, 1);
    assert(count == 2 && handles[0] == 0x300);
    assert(horizon_server_window_parents_locked(horizon_server_find_window_locked(DESKTOP), handles, 4) == 0);

    /* A window cannot go under itself or its own child, nor the desktop anywhere. */
    assert(horizon_server_set_parent_locked(horizon_server_find_window_locked(0x300),
                                            horizon_server_find_window_locked(0x200)) == HORIZON_STATUS_INVALID_PARAMETER);
    assert(horizon_server_set_parent_locked(horizon_server_find_window_locked(0x300),
                                            horizon_server_find_window_locked(0x300)) == HORIZON_STATUS_INVALID_PARAMETER);
    assert(horizon_server_set_parent_locked(horizon_server_find_window_locked(DESKTOP),
                                            horizon_server_find_window_locked(0x300)) == HORIZON_STATUS_INVALID_PARAMETER);
    expect("movie window children after refusals", children(0x300), "200 310");
    expect("top-level windows after refusals", children(DESKTOP), "300 100");

    /* SetParent(video, NULL) goes back to the desktop, at the top below topmost windows. */
    assert(horizon_server_set_parent_locked(horizon_server_find_window_locked(0x200),
                                            horizon_server_find_window_locked(DESKTOP)) == HORIZON_STATUS_SUCCESS);
    expect("top-level windows after going back", children(DESKTOP), "300 100 200");
    expect("movie window children after going back", children(0x300), "310");

    printf("PASS: SetParent and the parent list\n");
    return 0;
}
'''

source = ('#include <stdint.h>\n' + defines + fixture +
          function(horizon, 'static void horizon_server_link_window_locked') + '\n' +
          function(horizon, 'static unsigned int horizon_server_set_parent_locked') + '\n' +
          function(horizon, 'static unsigned int horizon_server_window_parents_locked') + '\n' + tests)
with tempfile.TemporaryDirectory() as tmp:
    c_file = Path(tmp) / 'window_parent.c'
    exe = Path(tmp) / 'window_parent'
    c_file.write_text(source)
    subprocess.run(['cc', '-std=gnu99', '-Wall', '-Wextra', '-Werror', '-o', str(exe), str(c_file)], check=True)
    subprocess.run([str(exe)], check=True)
