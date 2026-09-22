#ifndef WINE_NX_AUTORUN_UPDATE_H
#define WINE_NX_AUTORUN_UPDATE_H

#include <stddef.h>

enum autorun_update_result
{
    AUTORUN_UPDATE_OK,
    AUTORUN_UPDATE_CANCELLED,
    AUTORUN_UPDATE_NETWORK,
    AUTORUN_UPDATE_NOT_FOUND,
    AUTORUN_UPDATE_INVALID,
    AUTORUN_UPDATE_IO,
    AUTORUN_UPDATE_HASH
};

struct autorun_release
{
    char tag[64];
    char name[128];
    char published[32];
    char notes[16384];
    char url[768];
    char digest[65];
    unsigned long long size;
};

typedef int (*autorun_update_progress)( void *opaque, unsigned long long current,
                                        unsigned long long total );

enum autorun_update_result autorun_update_check( struct autorun_release *release,
        autorun_update_progress progress, void *opaque );
enum autorun_update_result autorun_update_download( const char *runtime_dir,
        const struct autorun_release *release, char *path, size_t path_size,
        autorun_update_progress progress, void *opaque );
const char *autorun_update_error( enum autorun_update_result result );

#endif
