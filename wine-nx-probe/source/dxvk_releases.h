#ifndef WINE_NX_DXVK_RELEASES_H
#define WINE_NX_DXVK_RELEASES_H

#include <stddef.h>

#define DXVK_MAX_RELEASES 128

struct dxvk_release
{
    char version[32];
    char url[768];
    char digest[65];
    unsigned long long size;
    int prerelease;
};

enum dxvk_result
{
    DXVK_OK,
    DXVK_NETWORK_ERROR,
    DXVK_NOT_FOUND,
    DXVK_INVALID_RESPONSE,
    DXVK_INVALID_ARCHIVE,
    DXVK_HASH_MISMATCH,
    DXVK_IO_ERROR
};

enum dxvk_progress_stage
{
    DXVK_PROGRESS_DOWNLOAD,
    DXVK_PROGRESS_VERIFY,
    DXVK_PROGRESS_INSTALL
};

typedef void (*dxvk_progress_callback)( void *opaque, enum dxvk_progress_stage stage,
                                        unsigned long long current, unsigned long long total );

enum dxvk_result dxvk_release_catalog( const char *runtime_dir, struct dxvk_release *releases,
                                       int max_releases, int *count, int refresh, int *cached );
enum dxvk_result dxvk_install_release( const char *runtime_dir, const struct dxvk_release *release,
                                       dxvk_progress_callback progress, void *opaque );
int dxvk_release_installed( const char *runtime_dir, unsigned short machine, const char *version );
int dxvk_root_version( const char *runtime_dir, unsigned short machine, char *version, size_t size );
const char *dxvk_result_message( enum dxvk_result result );

#endif
