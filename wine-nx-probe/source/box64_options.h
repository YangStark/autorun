/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later.
 *
 * A program's Box64 options file: SPEED2.EXE reads SPEED2.box64.txt next to it
 * (wow64_box64_dynarec.c). NAME=VALUE lines, # starts a comment. Shared with
 * tests/box64_options.c.
 */
#ifndef WINE_NX_BOX64_OPTIONS_H
#define WINE_NX_BOX64_OPTIONS_H

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* The option on one line; 0 for a blank line, a comment or anything else. */
static inline int nx_box64_option_line( const char *line, char *name, size_t size, long *value )
{
    const char *end, *equals;
    char *number_end;
    size_t length;

    while (isspace( (unsigned char)*line )) line++;
    if (!*line || *line == '#' || !(equals = strchr( line, '=' ))) return 0;
    for (end = equals; end > line && isspace( (unsigned char)end[-1] ); end--) continue;
    length = end - line;
    if (!length || length >= size) return 0;
    memcpy( name, line, length );
    name[length] = 0;
    *value = strtol( equals + 1, &number_end, 0 );
    if (number_end == equals + 1) return 0;
    while (isspace( (unsigned char)*number_end )) number_end++;
    return !*number_end || *number_end == '#';
}

#endif
