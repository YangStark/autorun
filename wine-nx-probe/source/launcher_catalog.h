#ifndef WINE_NX_LAUNCHER_CATALOG_H
#define WINE_NX_LAUNCHER_CATALOG_H

#include <stddef.h>
#include "launcher_list.h"

#define LAUNCHER_CATALOG_VERSION 3
#define LAUNCHER_CATALOG_FILE "launcher-library-v2.ini"

struct launcher_catalog_entry
{
    unsigned int id;
    unsigned int added_order;
    unsigned int launched_order;
    int favorite;
    char path[512];
    char title[128];
    char square_art[512];
    char portrait_art[512];
    char hero_art[512];
};

struct launcher_catalog
{
    struct launcher_catalog_entry entries[LAUNCHER_MAX_ENTRIES];
    int count;
    unsigned int next_id;
    unsigned int next_order;
};

enum launcher_catalog_result
{
    LAUNCHER_CATALOG_OK,
    LAUNCHER_CATALOG_MISSING,
    LAUNCHER_CATALOG_INVALID,
    LAUNCHER_CATALOG_IO_ERROR
};

void launcher_catalog_init( struct launcher_catalog *catalog );
enum launcher_catalog_result launcher_catalog_load( struct launcher_catalog *catalog, const char *path );
int launcher_catalog_save( const struct launcher_catalog *catalog, const char *path );
int launcher_catalog_find( const struct launcher_catalog *catalog, const char *path );
int launcher_catalog_add( struct launcher_catalog *catalog, const char *path, const char *title );
void launcher_catalog_remove( struct launcher_catalog *catalog, int index );
int launcher_catalog_import_legacy( struct launcher_catalog *catalog, const char *path );

#endif
