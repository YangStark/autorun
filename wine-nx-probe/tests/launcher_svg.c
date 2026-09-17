/*
 * Host check of source/launcher_svg.c: exact coverage for a square, the area of
 * arcs and curves, holes cut by the nonzero rule, the relative, implicit and
 * compact forms of path data, and parse failures. With a folder as its argument
 * it also writes the embedded icons there as PGM files to look at.
 */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../source/launcher_icons.h"
#include "../source/launcher_svg.h"

static unsigned char mask[256 * 256];

static double area( const char *d, float view, int size )
{
    double sum = 0;
    int i;

    assert( svg_path_mask( d, 0, 0, view, view, size, size, mask ) );
    for (i = 0; i < size * size; i++) sum += mask[i] / 255.0;
    return sum;
}

static void write_pgm( const char *folder, const char *name, const struct launcher_svg_icon *icon, int size )
{
    char path[512];
    FILE *file;

    assert( svg_path_mask( icon->d, icon->x, icon->y, icon->width, icon->height, size, size, mask ) );
    snprintf( path, sizeof(path), "%s/%s.pgm", folder, name );
    assert( (file = fopen( path, "wb" )) );
    fprintf( file, "P5 %d %d 255\n", size, size );
    fwrite( mask, 1, (size_t)size * size, file );
    fclose( file );
}

int main( int argc, char **argv )
{
    int x, y;

    /* A square on pixel boundaries covers its pixels fully and nothing else. */
    assert( svg_path_mask( "M2 2H6V6H2Z", 0, 0, 8, 8, 8, 8, mask ) );
    for (y = 0; y < 8; y++)
        for (x = 0; x < 8; x++)
            assert( mask[y * 8 + x] == (x >= 2 && x < 6 && y >= 2 && y < 6 ? 255 : 0) );
    /* Half a pixel is half covered. */
    assert( svg_path_mask( "M0 0h1.5v1H0z", 0, 0, 2, 1, 2, 1, mask ) );
    assert( mask[0] == 255 && mask[1] == 127 );

    /* A circle of two arcs, a curve-approximated circle, and an unclosed path fill the same. */
    assert( fabs( area( "M10 50a40 40 0 1 1 80 0a40 40 0 1 1-80 0", 100, 100 ) - M_PI * 1600 ) < 16 );
    assert( fabs( area( "M50 10C72 10 90 28 90 50S72 90 50 90 10 72 10 50 28 10 50 10", 100, 100 ) - M_PI * 1600 ) < 40 );
    assert( fabs( area( "M10 10h80v80h-80", 100, 100 ) - 6400 ) < 1 );
    /* A quadratic curve and its smooth continuation. */
    assert( area( "M0 50Q25 0 50 50T100 50Z", 100, 100 ) > 500 );

    /* An inner square wound the other way is a hole; wound the same way it is not. */
    assert( fabs( area( "M0 0h100v100H0zM25 25v50h50V25z", 100, 100 ) - 7500 ) < 1 );
    assert( fabs( area( "M0 0h100v100H0zM25 25h50v50H25z", 100, 100 ) - 10000 ) < 1 );

    /* Compact numbers ("0-64", ".5.5") and arc flags run together ("0110"). */
    assert( fabs( area( "M20 20l60 0 0 60-60 0z", 100, 100 ) - 3600 ) < 1 );
    assert( fabs( area( "M.5.5h99v99h-99z", 100, 100 ) - 9801 ) < 1 );
    assert( fabs( area( "M10 50a40 40 0 1180 0a40 40 0 11-80 0", 100, 100 ) - M_PI * 1600 ) < 16 );

    /* A view box that is not square is centred, and its offset is honoured. */
    assert( svg_path_mask( "M100 100h10v10h-10z", 100, 100, 10, 20, 20, 20, mask ) );
    assert( mask[0] == 0 && mask[5 * 20 + 5] == 255 && mask[5 * 20 + 15] == 0 );

    /* What does not parse fails and leaves nothing drawn. */
    assert( !svg_path_mask( "10 10 L 20 20", 0, 0, 30, 30, 30, 30, mask ) );
    assert( !svg_path_mask( "M10 10 L 20", 0, 0, 30, 30, 30, 30, mask ) );
    assert( !svg_path_mask( "M10 10 A 5 5 0 2 0 20 20", 0, 0, 30, 30, 30, 30, mask ) );
    for (x = 0; x < 900; x++) assert( !mask[x] );

    /* The embedded icons fill something at the sizes the launcher draws. */
    assert( area( icon_gamepad_modern.d, 576, 30 ) > 100 && area( icon_grid.d, 512, 30 ) > 100 );
    if (argc > 1)
    {
        write_pgm( argv[1], "gamepad-modern", &icon_gamepad_modern, 128 );
        write_pgm( argv[1], "grid", &icon_grid, 128 );
    }
    puts( "launcher SVG paths: coverage, arcs, curves, holes, compact data, view boxes and failures passed" );
    return 0;
}
