/*
 * The runtime's launcher (launcher.c), shown before Wine starts.
 */
#ifndef WINE_NX_LAUNCHER_H
#define WINE_NX_LAUNCHER_H

#include <stddef.h>

struct wine_nx_launcher_options
{
    const char *runtime_dir;   /* sdmc:/switch/wine: target.txt, args.txt, verbose.txt... */
    const char *build;
    /* 0 with the program's IMAGE_FILE_MACHINE_* when this runtime can start it. */
    int (*machine_of)( const char *path, unsigned short *machine );
    int vulkan;                /* the runtime has Vulkan, so DXVK's d3d9 can run */
    /* How wide an address space Horizon gave this process: 32 when the low 4 GB
     * is all of it, 36 or 39 when it reaches beyond, 0 when it could not be
     * read. The title that started the process fixes it, so a program that needs
     * the low 4 GB has to be opened from a forwarder that asks for 32 bits. */
    int address_space_bits;
    /* The global settings on entry, as the user left them on return. */
    int verbose;
    int profile;
    int framebuffer;
};

/* Show the launcher. Returns 1 with the chosen program's path in target, or 0
 * when the user quits. target on entry preselects a program. */
int wine_nx_launcher_run( struct wine_nx_launcher_options *options, char *target, size_t target_size );

/* A line in wine-nx-runtime.log (runtime.c). */
void wine_nx_runtime_trace( const char *msg );

/* What launcher_platform_status found. */
#define LAUNCHER_STATUS_CLOCK   1
#define LAUNCHER_STATUS_BATTERY 2

/* The time of day and the battery charge shown in the header. Returns the
 * LAUNCHER_STATUS_* bits for what it could read; the rest is left alone. */
int launcher_platform_status( int *hour, int *minute, int *battery, int *charging );

#ifndef __SWITCH__
/* A host build (tests/launcher_host.c) supplies what the Switch build takes from libnx. */
int launcher_platform_font( const void **data, size_t *size );
int launcher_platform_prompt( const char *header, const char *initial, char *out, size_t size );
#endif

#endif
