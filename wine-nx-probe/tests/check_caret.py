#!/usr/bin/env python3
"""Drive the Horizon server's caret helpers with win32u's caret logic and track the inverted pixels."""
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

defines = '\n'.join(re.findall(r'^#define HORIZON_(?:SET_CARET|CARET_STATE)_\w+ .*$', horizon, re.M))
fixture = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define HORIZON_STATUS_SUCCESS 0
#define HORIZON_STATUS_ACCESS_DENIED 0xc0000022u
struct horizon_rectangle { int left, top, right, bottom; };
struct horizon_input_shm { unsigned int caret; struct horizon_rectangle caret_rect; };
static int horizon_caret_hide;
static int horizon_caret_state;
'''
tests = r'''
enum { EDIT = 0x10042, OTHER = 0x10050, W = 64, H = 32 };
static struct horizon_input_shm input;
static unsigned char inverted[H][W];  /* SRCINVERT parity per pixel */

static void display_caret( unsigned int hwnd, struct horizon_rectangle r )
{
    assert( hwnd );
    for (int y = r.top; y < r.bottom; y++)
        for (int x = r.left; x < r.right; x++) inverted[y][x] ^= 1;
}

static int lit(void)
{
    int n = 0;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) n += inverted[y][x];
    return n;
}

struct old { unsigned int hwnd; struct horizon_rectangle rect; int hide, state; unsigned int err; };

static struct old info( unsigned int flags, unsigned int hwnd, int x, int y, int hide, int state )
{
    struct old old = { input.caret, input.caret_rect, horizon_caret_hide, horizon_caret_state };
    old.err = horizon_server_set_caret_info_locked( &input, flags, hwnd, x, y, hide, state );
    return old;
}

/* The sequences below follow dlls/win32u/input.c. */
static void create_caret( unsigned int hwnd, int width, int height )
{
    struct old old = { input.caret, input.caret_rect, horizon_caret_hide, horizon_caret_state };
    horizon_server_set_caret_window_locked( &input, hwnd, width, height );
    if (old.hwnd && !old.hide && old.state) display_caret( old.hwnd, old.rect );
}

static void set_caret_pos( int x, int y )
{
    struct old old = info( HORIZON_SET_CARET_POS | HORIZON_SET_CARET_STATE, 0, x, y, 0,
                           HORIZON_CARET_STATE_ON_IF_MOVED );
    struct horizon_rectangle r = old.rect;
    assert( !old.err );
    if (old.hide || (x == r.left && y == r.top)) return;
    if (old.state) display_caret( old.hwnd, r );
    r.right += x - r.left; r.bottom += y - r.top; r.left = x; r.top = y;
    display_caret( old.hwnd, r );
}

static int show_caret( unsigned int hwnd )
{
    struct old old = info( HORIZON_SET_CARET_HIDE | HORIZON_SET_CARET_STATE, hwnd, 0, 0, -1, HORIZON_CARET_STATE_ON );
    if (old.err) return 0;
    if (old.hide == 1) display_caret( old.hwnd, old.rect );
    return 1;
}

static void hide_caret( unsigned int hwnd )
{
    struct old old = info( HORIZON_SET_CARET_HIDE | HORIZON_SET_CARET_STATE, hwnd, 0, 0, 1, HORIZON_CARET_STATE_OFF );
    assert( !old.err );
    if (!old.hide && old.state) display_caret( old.hwnd, old.rect );
}

static void toggle_caret( unsigned int hwnd )  /* WM_SYSTIMER SYSTEM_TIMER_CARET */
{
    struct old old = info( HORIZON_SET_CARET_STATE, hwnd, 0, 0, 0, HORIZON_CARET_STATE_TOGGLE );
    if (!old.err && !old.hide) display_caret( old.hwnd, old.rect );
}

static void destroy_caret(void)
{
    struct old old = { input.caret, input.caret_rect, horizon_caret_hide, horizon_caret_state };
    horizon_server_set_caret_window_locked( &input, 0, 0, 0 );
    if (old.hwnd && !old.hide && old.state) display_caret( old.hwnd, old.rect );
}

int main(void)
{
    /* The edit control gets focus: a 1x13 caret at the text position, then shown. */
    create_caret( EDIT, 1, 13 );
    assert( input.caret == EDIT && !lit() );
    set_caret_pos( 5, 3 );
    assert( !lit() && input.caret_rect.left == 5 && input.caret_rect.bottom == 16 );
    assert( show_caret( EDIT ) && lit() == 13 && inverted[3][5] && inverted[15][5] );
    /* Blinking. */
    toggle_caret( EDIT ); assert( !lit() );
    toggle_caret( EDIT ); assert( lit() == 13 );
    /* Typing moves it: the old bar is erased and the new one drawn. */
    set_caret_pos( 20, 3 );
    assert( lit() == 13 && !inverted[3][5] && inverted[3][20] );
    /* Moving while blinked off draws it at the new place. */
    toggle_caret( EDIT ); assert( !lit() );
    set_caret_pos( 30, 10 );
    assert( lit() == 13 && inverted[10][30] && inverted[22][30] );
    /* Another window cannot show or hide this caret. */
    assert( !show_caret( OTHER ) && lit() == 13 );
    /* Nested hide and show. */
    hide_caret( EDIT ); assert( !lit() );
    hide_caret( EDIT ); toggle_caret( EDIT ); assert( !lit() );
    assert( show_caret( EDIT ) && !lit() );
    assert( show_caret( EDIT ) && lit() == 13 );
    /* The same window recreating its caret keeps the position, hidden. */
    create_caret( EDIT, 2, 13 );
    assert( !lit() && input.caret_rect.left == 30 && input.caret_rect.right == 32 );
    assert( show_caret( EDIT ) && lit() == 26 );
    /* Focus moves to another window: its caret starts at 0,0 and the old one is erased. */
    create_caret( OTHER, 1, 8 );
    assert( !lit() && input.caret == OTHER && !input.caret_rect.left && input.caret_rect.bottom == 8 );
    assert( show_caret( OTHER ) && lit() == 8 );
    destroy_caret();
    assert( !lit() && !input.caret );
    puts( "PASS: caret show, blink, move, nested hide, recreate, focus change and destroy leave the right pixels lit" );
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='wine-nx-caret-') as temp:
    src = Path(temp) / 'test.c'
    exe = Path(temp) / 'test'
    src.write_text(fixture + defines + '\n' +
                   function(horizon, 'static void horizon_server_set_caret_window_locked(') + '\n' +
                   function(horizon, 'static unsigned int horizon_server_set_caret_info_locked(') + tests)
    subprocess.run(['cc', '-g', '-Wall', '-Werror', '-fsanitize=address,undefined', str(src), '-o', str(exe)],
                   check=True)
    subprocess.run([str(exe)], check=True)
