#!/usr/bin/env python3
"""Run the NX driver's pointer event translation against scripted controller states."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'dlls/win32u/winnx_drv.c').read_text()
defines = source[source.index('/* Buttons reported by wine_nx_pointer_poll(). */'):source.index('struct wine_nx_surface\n')]
events = source[source.index('BOOL wine_nx_drv_ProcessEvents('):source.index('/**********************************************************************\n *           wine_nx_drv_CreateWindow')]
fixture = r'''
#include <assert.h>
#include <stdio.h>
typedef int BOOL, INT;
typedef unsigned int UINT, DWORD;
typedef void *HWND;
typedef long LPARAM;
#define TRUE 1
#define FALSE 0
#define INPUT_MOUSE 0
#define MOUSEEVENTF_MOVE 0x0001
#define MOUSEEVENTF_LEFTDOWN 0x0002
#define MOUSEEVENTF_LEFTUP 0x0004
#define MOUSEEVENTF_RIGHTDOWN 0x0008
#define MOUSEEVENTF_RIGHTUP 0x0010
#define MOUSEEVENTF_ABSOLUTE 0x8000
typedef struct { int dx, dy; DWORD mouseData, dwFlags, time; unsigned long dwExtraInfo; } MOUSEINPUT;
typedef struct { DWORD type; MOUSEINPUT mi; } INPUT;
static struct { int moved, x, y; unsigned int buttons; } state;
static INPUT sent[32];
static int sent_count, presents, set_x = -1, set_y = -1;
static int wine_nx_pointer_poll(int *x, int *y, unsigned int *buttons) {
    *x = state.x; *y = state.y; *buttons = state.buttons; return state.moved;
}
static void wine_nx_pointer_set_pos(int x, int y) { set_x = x; set_y = y; }
static void wine_nx_fb_present(void) { presents++; }
static void nxdrv_trace(const char *fmt, int a, int b, int c, int d) {}
static void nxdrv_trace_hot(const char *fmt, int a, int b, int c, int d) {}
static UINT NtUserSendHardwareInput(HWND hwnd, UINT flags, const INPUT *input, LPARAM lparam) {
    assert(!hwnd && !flags && !lparam && input->type == INPUT_MOUSE && sent_count < 32);
    sent[sent_count++] = *input; return TRUE;
}
'''
tests = r'''
/* Poll once with the given runtime state; return the flags sent, or -1 for none. */
static int poll(int moved, int x, int y, unsigned int buttons) {
    int before = sent_count;
    BOOL ret;
    state.moved = moved; state.x = x; state.y = y; state.buttons = buttons;
    ret = wine_nx_drv_ProcessEvents(0);
    assert(sent_count - before <= 1);
    assert(ret == (sent_count != before));
    if (sent_count == before) return -1;
    assert(sent[before].mi.dx == x && sent[before].mi.dy == y);
    return (int)sent[before].mi.dwFlags;
}
int main(void) {
    enum { L = WINE_NX_POINTER_LEFT, R = WINE_NX_POINTER_RIGHT, ABS = MOUSEEVENTF_ABSOLUTE };
    assert(poll(0, 640, 360, 0) == -1);
    assert(presents == 2);  /* before and after polling */
    assert(poll(1, 700, 360, 0) == (ABS | MOUSEEVENTF_MOVE));
    /* A clicks where the cursor is, without a spurious move. */
    assert(poll(0, 700, 360, L) == (ABS | MOUSEEVENTF_LEFTDOWN));
    assert(poll(0, 700, 360, L) == -1);
    /* Holding A while the stick moves drags. */
    assert(poll(1, 720, 380, L) == (ABS | MOUSEEVENTF_MOVE));
    assert(poll(0, 720, 380, L | R) == (ABS | MOUSEEVENTF_RIGHTDOWN));
    assert(poll(0, 720, 380, R) == (ABS | MOUSEEVENTF_LEFTUP));
    assert(poll(0, 720, 380, 0) == (ABS | MOUSEEVENTF_RIGHTUP));
    /* B alone is a right click. */
    assert(poll(0, 720, 380, R) == (ABS | MOUSEEVENTF_RIGHTDOWN));
    assert(poll(0, 720, 380, 0) == (ABS | MOUSEEVENTF_RIGHTUP));
    /* A touch elsewhere moves and presses in one event; lifting releases. */
    assert(poll(1, 100, 50, L) == (ABS | MOUSEEVENTF_MOVE | MOUSEEVENTF_LEFTDOWN));
    assert(poll(0, 100, 50, 0) == (ABS | MOUSEEVENTF_LEFTUP));
    assert(wine_nx_drv_SetCursorPos(12, 34) && set_x == 12 && set_y == 34);
    puts("PASS: stick moves, A left and B right clicks, drag, touch press and SetCursorPos");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='wine-nx-pointer-') as temp:
    src = Path(temp) / 'test.c'
    exe = Path(temp) / 'test'
    src.write_text(fixture + defines + events + tests)
    subprocess.run(['cc', '-g', '-Wall', '-Werror', '-Wno-unused-function', '-fsanitize=address,undefined',
                    str(src), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
