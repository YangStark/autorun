/*
 * The program list of the runtime's launcher: which files count as Windows
 * programs, their order, which one to start on, whether args.txt belongs to
 * the chosen program, and which rows are visible.
 */
#ifndef WINE_NX_LAUNCHER_LIST_H
#define WINE_NX_LAUNCHER_LIST_H

#include <stddef.h>
#include <string.h>
#include <strings.h>

#define LAUNCHER_MAX_ENTRIES 256

struct launcher_entry
{
    char path[512];          /* sdmc:/switch/wine/drive_c/... */
    char dos[256];           /* C:\... */
    unsigned short machine;  /* IMAGE_FILE_MACHINE_* */
};

static inline int launcher_is_exe( const char *name )
{
    size_t len = strlen( name );

    return name[0] != '.' && len > 4 && !strcasecmp( name + len - 4, ".exe" );
}

static inline int launcher_compare( const void *a, const void *b )
{
    const struct launcher_entry *x = a, *y = b;

    return strcasecmp( x->dos, y->dos );
}

/* args.txt holds a whole command line. It belongs to the program when its
 * first word, quoted or not, names that program. */
static inline int launcher_args_match( const char *args, const char *dos )
{
    size_t len = strlen( dos );

    while (*args == ' ' || *args == '\t') args++;
    if (*args == '"')
    {
        args++;
        return !strncasecmp( args, dos, len ) && args[len] == '"';
    }
    return !strncasecmp( args, dos, len ) && (!args[len] || args[len] == ' ' || args[len] == '\t');
}

/* A program's own arguments live next to it: openttd.exe reads
 * openttd.args.txt. Returns 0 when the path does not end in .exe or does not fit. */
static inline int launcher_args_path( const char *exe_path, char *out, size_t size )
{
    size_t len = strlen( exe_path );

    if (len < 4 || strcasecmp( exe_path + len - 4, ".exe" ) || len - 4 + sizeof(".args.txt") > size) return 0;
    memcpy( out, exe_path, len - 4 );
    memcpy( out + len - 4, ".args.txt", sizeof(".args.txt") );
    return 1;
}

/* The command line for a program with its own arguments; a path with spaces is quoted. */
static inline int launcher_command_line( const char *dos, const char *args, char *out, size_t size )
{
    const char *quote = strchr( dos, ' ' ) ? "\"" : "";
    int len = snprintf( out, size, "%s%s%s %s", quote, dos, quote, args );

    return len > 0 && (size_t)len < size;
}

/* The entry named by target.txt (a path or a C:\ path), or the first. */
static inline int launcher_find( const struct launcher_entry *entries, int count, const char *target )
{
    int i;

    for (i = 0; i < count; i++)
        if (!strcasecmp( entries[i].path, target ) || !strcasecmp( entries[i].dos, target )) return i;
    return 0;
}

/* The first visible row, moved only as far as needed to show "selected". */
static inline int launcher_first_visible( int first, int selected, int count, int rows )
{
    if (selected < first) first = selected;
    if (selected >= first + rows) first = selected - rows + 1;
    if (first > count - rows) first = count - rows;
    if (first < 0) first = 0;
    return first;
}

#endif
