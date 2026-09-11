/*
 * Splits a program's standard output bytes into log lines.
 *
 * Horizon has no console, so the runtime copies what a program writes to its
 * standard handles into the log. Terminal control is approximated: CR and LF
 * end a line (each CR-rewritten progress version is kept), backspace moves the
 * cursor left so later text overwrites, and a partial line such as a prompt or
 * progress indicator is emitted after it has been idle for a few ticks.
 */
#ifndef WINE_NX_STD_STREAM_LINES_H
#define WINE_NX_STD_STREAM_LINES_H

#include <stddef.h>

#define STD_STREAM_LINE_MAX 512

struct std_stream_lines
{
    unsigned int len;         /* characters in line */
    unsigned int cursor;      /* next write position, <= len */
    unsigned int idle;        /* ticks since the last byte */
    unsigned int lines;       /* lines emitted */
    unsigned long long bytes; /* bytes fed */
    char line[STD_STREAM_LINE_MAX];
};

typedef void (*std_stream_emit_fn)( void *ctx, const char *line );

static inline void std_stream_lines_emit( struct std_stream_lines *s, std_stream_emit_fn emit, void *ctx )
{
    /* Erased progress leaves spaces behind. */
    while (s->len && s->line[s->len - 1] == ' ') s->len--;
    if (s->len)
    {
        s->line[s->len] = 0;
        emit( ctx, s->line );
        s->lines++;
    }
    s->len = s->cursor = 0;
}

static inline void std_stream_lines_feed( struct std_stream_lines *s, const char *data, size_t size,
                                          std_stream_emit_fn emit, void *ctx )
{
    size_t i;

    s->bytes += size;
    s->idle = 0;
    for (i = 0; i < size; i++)
    {
        unsigned char c = data[i];

        if (c == '\n' || c == '\r') std_stream_lines_emit( s, emit, ctx );
        else if (c == '\b')
        {
            if (s->cursor) s->cursor--;
        }
        else if (c >= 0x20 || c == '\t')
        {
            if (c == 0x7f) continue;
            /* Split overlong lines rather than dropping their tail. */
            if (s->cursor == STD_STREAM_LINE_MAX - 1) std_stream_lines_emit( s, emit, ctx );
            s->line[s->cursor++] = c;
            if (s->cursor > s->len) s->len = s->cursor;
        }
    }
}

/* Called periodically: emits a pending partial line after idle_limit quiet ticks. */
static inline void std_stream_lines_tick( struct std_stream_lines *s, unsigned int idle_limit,
                                          std_stream_emit_fn emit, void *ctx )
{
    if (s->len && ++s->idle >= idle_limit) std_stream_lines_emit( s, emit, ctx );
}

#endif /* WINE_NX_STD_STREAM_LINES_H */
