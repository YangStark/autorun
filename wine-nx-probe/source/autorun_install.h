#ifndef AUTORUN_INSTALL_H
#define AUTORUN_INSTALL_H

#include <stddef.h>

enum autorun_install_result
{
    AUTORUN_INSTALL_OK, AUTORUN_INSTALL_CANCELLED, AUTORUN_INSTALL_INVALID,
    AUTORUN_INSTALL_SPACE, AUTORUN_INSTALL_IO, AUTORUN_INSTALL_RECOVERY
};

typedef int (*autorun_install_progress)( void *opaque, const char *phase,
                                        unsigned long long current, unsigned long long total );

enum autorun_install_result autorun_install_archive( const char *runtime_dir, const char *archive,
    const char *tag, int require_amd64, autorun_install_progress progress, void *opaque );
/* -1: recovery failed, 0: no transaction, 1: installed, 2: rolled back. */
int autorun_install_recover( const char *runtime_dir, int restore );
int autorun_install_finish( const char *runtime_dir );
int autorun_installed_release( const char *runtime_dir, char *tag, size_t size );
const char *autorun_install_error( enum autorun_install_result result );

#endif
