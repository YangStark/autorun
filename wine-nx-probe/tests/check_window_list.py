#!/usr/bin/env python3
"""Run the Horizon server's window tree and window list helpers on a Font dialog's windows."""
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

fixture = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct horizon_user_window { unsigned int handle, parent, owner, tid, id; struct horizon_user_window *next; };
struct horizon_get_window_tree_reply {
    struct { unsigned int error, reply_size; } header;
    unsigned int parent, owner, next_sibling, prev_sibling, first_sibling, last_sibling, first_child, last_child;
};
static struct horizon_user_window *horizon_windows;
'''
tests = r'''
enum { DESKTOP = 0x10020 };
static struct horizon_user_window windows[64];
static unsigned int window_count;

/* horizon_server_create_window_locked() puts new windows at the head. */
static unsigned int create(unsigned int parent, unsigned int id, unsigned int tid)
{
    struct horizon_user_window *window = &windows[window_count++];
    window->handle = 0x10020 + 2 * (window_count - 1);
    window->parent = parent;
    window->id = id;
    window->tid = tid;
    window->next = horizon_windows;
    horizon_windows = window;
    return window->handle;
}

static struct horizon_user_window *find(unsigned int handle)
{
    for (unsigned int i = 0; i < window_count; i++) if (windows[i].handle == handle) return &windows[i];
    return NULL;
}

static struct horizon_get_window_tree_reply tree(unsigned int handle)
{
    struct horizon_get_window_tree_reply reply;
    memset(&reply, 0, sizeof(reply));
    horizon_server_window_tree_locked(find(handle), &reply);
    return reply;
}

/* user32 GetDlgItem: GW_CHILD, then the server's sibling list from that child. */
static unsigned int get_dlg_item(unsigned int dialog, unsigned int id, unsigned int *list, unsigned int *count)
{
    unsigned int first = tree(dialog).first_child;
    *count = 0;
    horizon_server_window_list_locked(dialog, find(first), 0, 0, list, count, 64);
    for (unsigned int i = 0; i < *count; i++) if (find(list[i])->id == id) return list[i];
    return 0;
}

int main(void)
{
    unsigned int controls[32], ncontrols = 0, list[64], count, hwnd, i;
    static const unsigned int ids[] = {0x440, 0x470, 0x441, 0x471, 0x442, 0x472, 0x430, 0x473, 0x474,
                                       0x1, 0x2, 0x40e, 0x401};

    assert(create(0, 0, 0) == DESKTOP);
    create(0, 0, 0);  /* message window */
    unsigned int notepad = create(DESKTOP, 0, 4);
    create(notepad, 0, 4);  /* its edit control */
    unsigned int dialog = create(DESKTOP, 0, 4);
    for (i = 0; i < sizeof(ids) / sizeof(ids[0]); i++)
    {
        controls[ncontrols++] = create(dialog, ids[i], 4);
        /* Comboboxes create their list and edit children right away. */
        if (ids[i] >= 0x470 && ids[i] <= 0x474)
        {
            create(controls[ncontrols - 1], 0x3e8, 4);
            create(controls[ncontrols - 1], 0x3e9, 4);
        }
    }

    /* Every control is found by ID, grandchildren are not siblings. */
    for (i = 0; i < ncontrols; i++)
    {
        assert(get_dlg_item(dialog, ids[i], list, &count) == controls[i]);
        assert(count == ncontrols);
    }
    assert(!get_dlg_item(dialog, 0x3e8, list, &count));

    /* GW_HWNDNEXT from the first child visits the same list, and GW_HWNDPREV reverses it. */
    get_dlg_item(dialog, 0x440, list, &count);
    hwnd = tree(dialog).first_child;
    for (i = 0; hwnd; i++, hwnd = tree(hwnd).next_sibling) assert(i < count && hwnd == list[i]);
    assert(i == count);
    hwnd = tree(dialog).last_child;
    for (i = count; hwnd; hwnd = tree(hwnd).prev_sibling) assert(i && hwnd == list[--i]);
    assert(!i);
    for (i = 0; i < count; i++)
    {
        struct horizon_get_window_tree_reply reply = tree(list[i]);
        assert(reply.parent == dialog && reply.first_sibling == list[0] && reply.last_sibling == list[count - 1]);
    }

    /* Siblings from a later control on. */
    count = 0;
    horizon_server_window_list_locked(dialog, find(list[3]), 0, 0, list, &count, 64);
    assert(count == ncontrols - 3);

    /* EnumChildWindows: each control followed by its own children. */
    count = 0;
    horizon_server_window_list_locked(dialog, NULL, 0, 1, list, &count, 64);
    assert(count == ncontrols + 10);
    for (i = 0; i < count; i++)
        if (find(list[i])->parent != dialog) assert(find(list[i])->parent == list[i - 1] || find(list[i - 1])->parent == find(list[i])->parent);

    /* A short buffer still reports the full count; a thread filter excludes other threads. */
    count = 0;
    horizon_server_window_list_locked(dialog, NULL, 0, 0, list, &count, 3);
    assert(count == ncontrols);
    count = 0;
    horizon_server_window_list_locked(DESKTOP, NULL, 9, 0, list, &count, 64);
    assert(count == 0);
    count = 0;
    horizon_server_window_list_locked(DESKTOP, NULL, 4, 0, list, &count, 64);
    assert(count == 2 && list[0] == dialog && list[1] == notepad);

    /* The desktop window has no siblings, only children. */
    assert(!tree(DESKTOP).next_sibling && !tree(DESKTOP).prev_sibling && !tree(DESKTOP).first_sibling);
    assert(tree(DESKTOP).first_child == dialog && tree(DESKTOP).last_child == notepad);
    puts("PASS: GetDlgItem finds every Font dialog control; sibling walks, recursive lists, counts and filters agree");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='wine-nx-window-list-') as temp:
    src = Path(temp) / 'test.c'
    exe = Path(temp) / 'test'
    src.write_text(fixture + '\n'.join(function(horizon, name) for name in (
        'static void horizon_server_window_tree_locked(',
        'static void horizon_server_append_window_locked(',
        'static void horizon_server_window_list_locked(')) + tests)
    subprocess.run(['cc', '-g', '-Wall', '-Werror', '-fsanitize=address,undefined', str(src), '-o', str(exe)],
                   check=True)
    subprocess.run([str(exe)], check=True)
