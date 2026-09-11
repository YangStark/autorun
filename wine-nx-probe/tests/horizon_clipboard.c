/* Host test for the Horizon server's clipboard (dlls/ntdll/unix/horizon_clipboard.h). */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../dlls/ntdll/unix/horizon_clipboard.h"

#define EDIT 0x10042
#define DIALOG 0x10070
#define VIEWER 0x10080

static const unsigned short text[] = { 'h', 'i', 0 };

static void test_copy_and_paste(void)
{
    struct horizon_clipboard clip = {0};
    struct horizon_clip_data_reply data;
    unsigned int owner, seqno, viewer, list[8], count, next;
    const unsigned char ansi[] = "hi";

    /* Edit > Copy: OpenClipboard, EmptyClipboard, SetClipboardData(CF_UNICODETEXT), CloseClipboard. */
    assert( !horizon_clip_open( &clip, 4, EDIT, &owner ) && !owner );
    assert( !horizon_clip_empty( &clip, 4 ) && clip.owner == EDIT );
    assert( !horizon_clip_set_data( &clip, 4, HORIZON_CLIP_CF_UNICODETEXT, 0x409, text, sizeof(text), &seqno ) );
    assert( horizon_clip_close_locked( &clip, &viewer ) && !viewer && !clip.open_tid );
    /* Text in other encodings and the locale are offered at close. */
    assert( !horizon_clip_formats( &clip, 0, list, 8, &count ) && count == 4 );
    assert( list[0] == HORIZON_CLIP_CF_UNICODETEXT && list[1] == HORIZON_CLIP_CF_LOCALE &&
            list[2] == HORIZON_CLIP_CF_TEXT && list[3] == HORIZON_CLIP_CF_OEMTEXT );
    assert( !horizon_clip_formats( &clip, HORIZON_CLIP_CF_TEXT, NULL, 0, &count ) && count == 1 );
    assert( horizon_clip_formats( &clip, 0, list, 3, &count ) == HORIZON_CLIP_STATUS_BUFFER_TOO_SMALL && count == 4 );

    /* Edit > Paste from another thread's edit control. */
    assert( !horizon_clip_open( &clip, 8, DIALOG, &owner ) && owner == EDIT );
    assert( !horizon_clip_get_data( &clip, 8, HORIZON_CLIP_CF_UNICODETEXT, 1, 0, 0, 64, &data ) );
    assert( data.total == sizeof(text) && !memcmp( data.data, text, sizeof(text) ) && data.owner == EDIT );
    assert( horizon_clip_get_data( &clip, 8, HORIZON_CLIP_CF_UNICODETEXT, 1, 0, 0, 2, &data ) ==
            HORIZON_CLIP_STATUS_BUFFER_OVERFLOW );
    /* The client's cached copy is still current: no data sent. */
    assert( !horizon_clip_get_data( &clip, 8, HORIZON_CLIP_CF_UNICODETEXT, 1, 1, seqno, 64, &data ) && !data.data );
    /* CF_TEXT is synthesized: win32u converts it, stores it without a new sequence, and asks again. */
    seqno = clip.seqno;
    assert( !horizon_clip_get_data( &clip, 8, HORIZON_CLIP_CF_TEXT, 1, 0, 0, 64, &data ) );
    assert( data.from == HORIZON_CLIP_CF_UNICODETEXT && !data.data && clip.rendering == 1 );
    assert( !horizon_clip_set_data( &clip, 8, HORIZON_CLIP_CF_TEXT, 0x409, ansi, sizeof(ansi), &next ) );
    assert( clip.seqno == seqno );
    assert( !horizon_clip_get_data( &clip, 8, HORIZON_CLIP_CF_TEXT, 0, 0, 0, 64, &data ) );
    assert( data.total == 3 && !memcmp( data.data, "hi", 3 ) && !data.from && !clip.rendering );
    assert( horizon_clip_get_data( &clip, 8, 0xc123, 1, 0, 0, 64, &data ) == HORIZON_CLIP_STATUS_OBJECT_NAME_NOT_FOUND );
    assert( !horizon_clip_enum( &clip, 8, 0, &next ) && next == HORIZON_CLIP_CF_UNICODETEXT );
    assert( !horizon_clip_enum( &clip, 8, HORIZON_CLIP_CF_TEXT, &next ) && next == HORIZON_CLIP_CF_OEMTEXT );
    assert( !horizon_clip_enum( &clip, 8, HORIZON_CLIP_CF_OEMTEXT, &next ) && !next );
    /* Reading does not change the clipboard: no notification at close. */
    assert( !horizon_clip_close_locked( &clip, &viewer ) );
    horizon_clip_free_formats( &clip );
}

static void test_open_rules(void)
{
    struct horizon_clipboard clip = {0};
    struct horizon_clip_data_reply data;
    unsigned int owner, seqno, next;

    assert( horizon_clip_empty( &clip, 4 ) == HORIZON_CLIP_STATUS_NOT_OPEN );
    assert( horizon_clip_set_data( &clip, 4, HORIZON_CLIP_CF_TEXT, 0, "x", 2, &seqno ) == HORIZON_CLIP_STATUS_NOT_OPEN );
    assert( horizon_clip_get_data( &clip, 4, HORIZON_CLIP_CF_TEXT, 1, 0, 0, 8, &data ) == HORIZON_CLIP_STATUS_NOT_OPEN );
    assert( horizon_clip_enum( &clip, 4, 0, &next ) == HORIZON_CLIP_STATUS_NOT_OPEN );
    assert( !horizon_clip_open( &clip, 4, EDIT, &owner ) );
    /* Held open by EDIT: another window cannot open it, the same window can again. */
    assert( horizon_clip_open( &clip, 8, DIALOG, &owner ) == HORIZON_CLIP_STATUS_INVALID_LOCK_SEQUENCE );
    assert( !horizon_clip_open( &clip, 4, EDIT, &owner ) );
    assert( horizon_clip_empty( &clip, 8 ) == HORIZON_CLIP_STATUS_NOT_OPEN );
    /* A thread that ends with it open closes it. */
    assert( !horizon_clip_thread_ended( &clip, 8 ) && clip.open_tid == 4 );
    horizon_clip_thread_ended( &clip, 4 );
    assert( !clip.open_tid && !clip.open_win );
    assert( !horizon_clip_open( &clip, 8, DIALOG, &owner ) );
    horizon_clip_close_locked( &clip, &owner );
}

static void test_delay_rendering_and_owner(void)
{
    struct horizon_clipboard clip = {0};
    struct horizon_clip_data_reply data;
    unsigned int owner, seqno, viewer, count, list[8];

    assert( !horizon_clip_open( &clip, 4, EDIT, &owner ) && !horizon_clip_empty( &clip, 4 ) );
    assert( !horizon_clip_set_data( &clip, 4, HORIZON_CLIP_CF_UNICODETEXT, 0, NULL, 0, &seqno ) );
    assert( !horizon_clip_set_data( &clip, 4, 0xc100, 0, NULL, 0, &seqno ) );
    horizon_clip_close_locked( &clip, &viewer );
    assert( clip.format_count == 5 );  /* the two, locale, CF_TEXT and CF_OEMTEXT */
    /* The owner renders on request (WM_RENDERFORMAT). */
    assert( !horizon_clip_open( &clip, 8, DIALOG, &owner ) );
    assert( !horizon_clip_get_data( &clip, 8, 0xc100, 1, 0, 0, 64, &data ) && !data.data && data.owner == EDIT );
    assert( clip.rendering == 1 );
    horizon_clip_close_locked( &clip, &viewer );
    /* Without an owner, what only it could render is dropped, and what derives from it too. */
    assert( horizon_clip_release_locked( &clip, &viewer ) && !clip.owner );
    assert( !horizon_clip_formats( &clip, 0, list, 8, &count ) && count == 1 );  /* the locale */
    assert( clip.formats->id == HORIZON_CLIP_CF_LOCALE );
    assert( !horizon_clip_release_locked( &clip, &viewer ) );
    horizon_clip_free_formats( &clip );
}

static void test_viewer_listeners_and_windows(void)
{
    struct horizon_clipboard clip = {0};
    unsigned int old, owner, seqno;

    assert( !horizon_clip_set_viewer( &clip, VIEWER, 0, &old, &owner ) && !old && clip.viewer == VIEWER );
    assert( horizon_clip_set_viewer( &clip, DIALOG, EDIT, &old, &owner ) == HORIZON_CLIP_STATUS_PENDING );
    assert( old == VIEWER && clip.viewer == VIEWER );
    assert( !horizon_clip_add_listener( &clip, EDIT ) && !horizon_clip_add_listener( &clip, DIALOG ) );
    assert( horizon_clip_add_listener( &clip, EDIT ) == HORIZON_CLIP_STATUS_INVALID_PARAMETER );
    for (unsigned int i = 0; i < 20; i++) assert( !horizon_clip_add_listener( &clip, 0x20000 + i ) );
    assert( clip.listen_count == 22 && clip.listeners[1] == DIALOG );
    assert( horizon_clip_remove_listener( &clip, DIALOG ) && !horizon_clip_remove_listener( &clip, DIALOG ) );
    assert( clip.listen_count == 21 && clip.listeners[1] == 0x20000 );

    /* The owning window is destroyed while holding the clipboard open after a change. */
    assert( !horizon_clip_open( &clip, 4, EDIT, &owner ) && !horizon_clip_empty( &clip, 4 ) );
    assert( !horizon_clip_set_data( &clip, 4, 0xc100, 0, NULL, 0, &seqno ) );
    assert( horizon_clip_window_destroyed( &clip, EDIT ) );
    assert( !clip.owner && !clip.open_tid && !clip.format_count && clip.listeners[0] == 0x20000 );
    assert( horizon_clip_window_destroyed( &clip, VIEWER ) == 0 && !clip.viewer );
    free( clip.listeners );
}

int main(void)
{
    test_copy_and_paste();
    test_open_rules();
    test_delay_rendering_and_owner();
    test_viewer_listeners_and_windows();
    puts( "Horizon clipboard: copy, paste, synthesized text, cache, open rules, delay rendering, owner release, "
          "viewer, listeners and cleanup passed" );
    return 0;
}
