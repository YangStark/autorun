/* Host test for the standard stream line splitter (source/std_stream_lines.h). */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../source/std_stream_lines.h"

struct capture
{
    char out[8192];
    size_t used;
    unsigned int count;
};

static void collect( void *ctx, const char *line )
{
    struct capture *capture = ctx;
    size_t len = strlen( line );

    assert( capture->used + len + 2 < sizeof(capture->out) );
    memcpy( capture->out + capture->used, line, len );
    capture->used += len;
    capture->out[capture->used++] = '|';
    capture->out[capture->used] = 0;
    capture->count++;
}

static void feed( struct std_stream_lines *s, const char *text, struct capture *c )
{
    std_stream_lines_feed( s, text, strlen( text ), collect, c );
}

static void test_crlf_and_blank_lines(void)
{
    struct std_stream_lines s = {0};
    struct capture c = {0};

    feed( &s, "\r\n7-Zip (r) 26.03 (x86)\r\n\r\nEverything is Ok\r\n", &c );
    assert( !strcmp( c.out, "7-Zip (r) 26.03 (x86)|Everything is Ok|" ) );
    assert( s.lines == 2 && s.bytes == 45 && !s.len );
}

static void test_split_writes(void)
{
    struct std_stream_lines s = {0};
    struct capture c = {0};

    /* One printf can arrive as several writes, a CRLF can straddle two. */
    feed( &s, "Files: ", &c );
    feed( &s, "2\r", &c );
    feed( &s, "\nSize:       325536", &c );
    assert( !strcmp( c.out, "Files: 2|" ) );
    std_stream_lines_emit( &s, collect, &c );
    assert( !strcmp( c.out, "Files: 2|Size:       325536|" ) );
}

static void test_cr_progress_and_backspace(void)
{
    struct std_stream_lines s = {0};
    struct capture c = {0};

    feed( &s, " 10%\r 55%\r", &c );
    assert( !strcmp( c.out, " 10%| 55%|" ) );
    c.used = 0; c.out[0] = 0;
    /* 7-Zip erases progress with backspace, space, backspace. */
    feed( &s, "  7% 1\b\b\b\b\b\b      \b\b\b\b\b\bTesting", &c );
    feed( &s, "\n", &c );
    assert( !strcmp( c.out, "Testing|" ) );
    c.used = 0; c.out[0] = 0;
    feed( &s, "\b\bab\bc\n", &c );
    assert( !strcmp( c.out, "ac|" ) );
}

static void test_controls_and_tabs(void)
{
    struct std_stream_lines s = {0};
    struct capture c = {0};

    feed( &s, "a\tb\x01\x7f" "c\xc3\xa9\n", &c );
    assert( !strcmp( c.out, "a\tbc\xc3\xa9|" ) );
}

static void test_overlong_line(void)
{
    struct std_stream_lines s = {0};
    struct capture c = {0};
    char text[1200];

    memset( text, 'x', 1100 );
    text[1100] = '\n';
    std_stream_lines_feed( &s, text, 1101, collect, &c );
    assert( c.count == 3 );
    assert( c.used == 1100 + 3 );
    assert( s.bytes == 1101 );
}

static void test_idle_flush(void)
{
    struct std_stream_lines s = {0};
    struct capture c = {0};
    unsigned int i;

    std_stream_lines_tick( &s, 5, collect, &c );
    assert( !c.count );
    feed( &s, "Enter password:", &c );
    for (i = 0; i < 4; i++) std_stream_lines_tick( &s, 5, collect, &c );
    assert( !c.count );
    feed( &s, " ", &c );  /* new data restarts the idle count */
    for (i = 0; i < 4; i++) std_stream_lines_tick( &s, 5, collect, &c );
    assert( !c.count );
    std_stream_lines_tick( &s, 5, collect, &c );
    assert( !strcmp( c.out, "Enter password:|" ) );
    std_stream_lines_tick( &s, 5, collect, &c );
    assert( c.count == 1 );
}

int main(void)
{
    test_crlf_and_blank_lines();
    test_split_writes();
    test_cr_progress_and_backspace();
    test_controls_and_tabs();
    test_overlong_line();
    test_idle_flush();
    printf( "std stream lines: CRLF, split writes, progress rewrite, controls, overlong lines, idle flush passed\n" );
    return 0;
}
