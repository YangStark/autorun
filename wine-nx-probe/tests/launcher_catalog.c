#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "../source/launcher_catalog.h"

int main(void)
{
    struct launcher_catalog written, read;
    char path[] = "/tmp/wine-nx-catalog-XXXXXX";
    char legacy[] = "/tmp/wine-nx-legacy-XXXXXX";
    FILE *file;
    int fd = mkstemp( path ), index;
    assert( fd >= 0 );
    close( fd );
    unlink( path );

    launcher_catalog_init( &written );
    index = launcher_catalog_add( &written, "sdmc:/Games/A=B/[One]/game.exe", "A %= Game" );
    assert( index == 0 );
    written.entries[index].favorite = 1;
    written.entries[index].launched_order = 8;
    assert( launcher_catalog_add( &written, "SDMC:/games/a=b/[one]/GAME.EXE", "duplicate" ) == 0 );
    assert( launcher_catalog_save( &written, path ) );
    assert( launcher_catalog_load( &read, path ) == LAUNCHER_CATALOG_OK );
    assert( read.count == 1 );
    assert( !strcmp( read.entries[0].path, "sdmc:/Games/A=B/[One]/game.exe" ) );
    assert( !strcmp( read.entries[0].title, "A %= Game" ) );
    assert( read.entries[0].favorite == 1 && read.entries[0].launched_order == 8 );

    launcher_catalog_remove( &read, 0 );
    assert( read.count == 0 );
    assert( launcher_catalog_save( &read, path ) );
    assert( launcher_catalog_load( &written, path ) == LAUNCHER_CATALOG_OK && written.count == 0 );

    fd = mkstemp( legacy );
    assert( fd >= 0 );
    close( fd );
    file = fopen( legacy, "w" );
    assert( file );
    assert( fprintf( file, "sdmc:/games/one.exe\nsdmc:/games/ONE.exe\nsdmc:/games/missing.exe\n" ) > 0 );
    assert( !fclose( file ) );
    launcher_catalog_init( &written );
    assert( launcher_catalog_import_legacy( &written, legacy ) );
    assert( written.count == 2 );
    assert( !strcmp( written.entries[1].path, "sdmc:/games/missing.exe" ) );
    unlink( path );
    unlink( legacy );
    return 0;
}
