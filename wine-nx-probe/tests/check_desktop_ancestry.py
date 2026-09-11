#!/usr/bin/env python3
"""Exercise synthetic desktop creation and Wine's real ancestor lookup on the host."""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
def read(path):
    if '--baseline' in sys.argv:
        return subprocess.check_output(['git', '-C', str(root), 'show', f'HEAD:{path}'], text=True)
    return (root / path).read_text()

def function(source, name):
    start = source.index(name)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

horizon = read('dlls/ntdll/unix/horizon.c')
window = read('dlls/win32u/window.c')
allocator = horizon
if '--allocator-baseline' in sys.argv:
    allocator = subprocess.check_output(['git', '-C', str(root), 'show',
                                        'HEAD:dlls/ntdll/unix/horizon.c'], text=True)
fixture = r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <stddef.h>
typedef uintptr_t HWND;
typedef unsigned int UINT, user_handle_t;
typedef int BOOL;
typedef struct { HWND parent; } WND;
#define WINAPI
#define WND_OTHER_PROCESS ((WND *)1)
#define WND_DESKTOP ((WND *)2)
#define GA_PARENT 1
#define GA_ROOT 2
#define GA_ROOTOWNER 3
#define ERROR_INVALID_WINDOW_HANDLE 1400
#define HORIZON_SERVER_OBJECT_DESKTOP 1
#define HORIZON_STATUS_INVALID_HANDLE 0xc0000008u
#define HORIZON_DESKTOP_ATOM 32769
#define HORIZON_NTUSER_DPI_PER_MONITOR_AWARE 3
#define NTUSER_OBJ_WINDOW 1
#define HORIZON_MAX_USER_HANDLES 32
#define HORIZON_FIRST_USER_HANDLE 0x20
#define HORIZON_STATUS_NO_MEMORY 0xc0000017u
struct horizon_obj_locator { unsigned long long id, offset; };
struct horizon_user_entry {
    unsigned long long offset;
    unsigned tid, pid;
    unsigned long long id;
    union { struct { unsigned short type, generation; }; long long uniq; };
};
struct horizon_session_shm { struct horizon_user_entry user_entries[HORIZON_MAX_USER_HANDLES]; };
static struct horizon_session_shm session;
static void *horizon_session_data = &session;
static unsigned horizon_user_handle_count;
static unsigned horizon_server_ensure_session_locked(void) { return 0; }
static unsigned horizon_server_flush_session_range_locked(unsigned long long offset, unsigned size) {
    assert(offset + size <= sizeof(session)); return 0;
}
static unsigned horizon_server_alloc_user_handle_locked(unsigned short, struct horizon_obj_locator,
                                                        unsigned, unsigned, unsigned *);
struct horizon_user_window { int unused; };
struct horizon_server_connection { unsigned pid, tid; int reply_fd; };
struct horizon_get_desktop_window_reply { struct { unsigned error; } header; HWND top_window, msg_window; };
struct horizon_server_object { HWND desktop_top_window, desktop_msg_window; };
struct horizon_atom_entry { unsigned atom; };
static struct horizon_server_object desktop;
static struct horizon_atom_entry message_atom = {32770};
static pthread_mutex_t horizon_server_objects_mutex = PTHREAD_MUTEX_INITIALIZER;
static unsigned horizon_thread_desktop = 5, desktop_pid, message_pid;
static WND main_win = {0x10020}, child_win = {0x10038}, popup_win = {0x10020};
static void *horizon_server_find_handle_object_locked(unsigned h, int t) { return &desktop; }
static void *horizon_server_find_atom_name_locked(const unsigned char *n, unsigned len) { return &message_atom; }
static unsigned horizon_server_create_window_locked(unsigned parent, unsigned owner, unsigned atom,
    unsigned long long ci, unsigned long long instance, unsigned dpi, unsigned style, unsigned ex,
    unsigned pid, unsigned tid, struct horizon_user_window **out) {
    unsigned handle;
    assert(!horizon_server_alloc_user_handle_locked(NTUSER_OBJ_WINDOW,
               (struct horizon_obj_locator){1, 0}, pid, tid, &handle));
    unsigned shared_pid = session.user_entries[(handle & 0xffff) / 2 - 0x10].pid;
    if (atom == HORIZON_DESKTOP_ATOM) { desktop.desktop_top_window = handle; desktop_pid = shared_pid; }
    else { desktop.desktop_msg_window = handle; message_pid = shared_pid; }
    return 0;
}
static int horizon_server_write_reply(int fd, const void *r, unsigned size, void *data, unsigned len) { return 0; }
#define horizon_trace(...) ((void)0)
static WND *get_user_handle_ptr(HWND h, unsigned type) {
    if (h == 0x10020) return desktop_pid == 1 ? NULL : WND_OTHER_PROCESS;
    if (h == 0x10022) return message_pid == 1 ? NULL : WND_OTHER_PROCESS;
    if (h == 0x10038) return &main_win;
    if (h == 0x10042) return &child_win;
    if (h == 0x10052) return &popup_win;
    return NULL;
}
static BOOL is_desktop_window(HWND h) { return h == 0x10020 || h == 0x10022; }
static void release_win_ptr(WND *w) {}
static void RtlSetLastWin32Error(unsigned e) {}
static HWND get_full_window_handle(HWND h) { return h; }
static HWND get_parent(HWND h) { return h == 0x10042 ? 0x10038 : 0; }
struct request { HWND handle; };
struct reply { HWND parent; int count; };
#define SERVER_START_REQ(name) { struct request request = {0}, *req = &request; struct reply response = {0}, *reply = &response;
#define SERVER_END_REQ }
#define wine_server_ptr_handle(h) (h)
#define wine_server_user_handle(h) (h)
static void wine_server_set_reply(void *r, void *data, unsigned size) { assert(!"unexpected server fallback"); }
static int wine_server_call(void *r) { assert(!"unexpected server fallback"); return 1; }
static int wine_server_call_err(void *r) { return wine_server_call(r); }
'''
code = fixture + '\n' + '\n'.join([
    function(allocator, 'static unsigned int horizon_server_alloc_user_handle_locked('),
    function(horizon, 'static int horizon_server_handle_get_desktop_window('),
    function(window, 'WND *get_win_ptr('),
    function(window, 'static HWND *list_window_parents('),
    function(window, 'HWND WINAPI NtUserGetAncestor('),
]) + r'''
int main(void) {
    struct horizon_server_connection connection = {1, 4, 0};
    horizon_server_handle_get_desktop_window(&connection, NULL);
    HWND root = NtUserGetAncestor(0x10038, GA_ROOT);
    printf("desktop pid=%u, Notepad root=%lx\n", desktop_pid, (unsigned long)root);
    fflush(stdout);
    assert(root == 0x10038);
    assert(NtUserGetAncestor(0x10042, GA_ROOT) == 0x10038);
    assert(NtUserGetAncestor(0x10052, GA_ROOT) == 0x10052);
    assert(NtUserGetAncestor(0x10038, GA_PARENT) == 0x10020);
    assert(NtUserGetAncestor(0x10020, GA_PARENT) == 0);
    assert(get_win_ptr(0x10022) == WND_DESKTOP);
    assert(session.user_entries[0].tid == 0 && session.user_entries[1].tid == 0);
    unsigned handle;
    assert(!horizon_server_alloc_user_handle_locked(NTUSER_OBJ_WINDOW,
               (struct horizon_obj_locator){2, 16}, 1, 4, &handle));
    assert(session.user_entries[2].pid == 1 && session.user_entries[2].tid == 4);
    assert(session.user_entries[2].id == 2 && session.user_entries[2].offset == 16);
    assert(handle == 0x10024);
    puts("PASS: shared handle allocator, server desktop metadata, main/child/popup ancestry, message parent");
}
'''
with tempfile.TemporaryDirectory(prefix='wine-nx-ancestry-') as temp:
    src = Path(temp) / 'test.c'
    exe = Path(temp) / 'test'
    src.write_text(code)
    subprocess.run(['cc', '-g', '-fsanitize=address,undefined', '-pthread', str(src), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
