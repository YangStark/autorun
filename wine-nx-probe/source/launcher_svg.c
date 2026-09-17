/*
 * Filling SVG paths for the launcher's icons (launcher_svg.h).
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "launcher_svg.h"

#define SAMPLES 4    /* on each axis of a pixel */

struct edge
{
    float x0, y0, x1, y1;    /* in samples; y0 != y1 */
};

struct path
{
    struct edge *edges;
    int count, capacity, failed;
    float scale, offset_x, offset_y;    /* from the view box to samples */
    float start_x, start_y, x, y;       /* in the view box */
};

static void line_to( struct path *path, float x, float y )
{
    struct edge edge =
    {
        path->x * path->scale + path->offset_x, path->y * path->scale + path->offset_y,
        x * path->scale + path->offset_x, y * path->scale + path->offset_y,
    };

    path->x = x;
    path->y = y;
    /* A level edge crosses no scanline. */
    if (edge.y0 == edge.y1 || path->failed) return;
    if (path->count == path->capacity)
    {
        int capacity = path->capacity ? path->capacity * 2 : 256;
        struct edge *edges = realloc( path->edges, capacity * sizeof(*edges) );

        if (!edges)
        {
            path->failed = 1;
            return;
        }
        path->edges = edges;
        path->capacity = capacity;
    }
    path->edges[path->count++] = edge;
}

/* A fill closes every subpath, whether or not it ends in Z. */
static void close_path( struct path *path )
{
    if (path->x != path->start_x || path->y != path->start_y) line_to( path, path->start_x, path->start_y );
}

/* Enough straight pieces that none strays more than a fraction of a sample. */
static int pieces( const struct path *path, float length )
{
    int count = (int)(sqrtf( length * path->scale ) * 1.5f);

    return count < 4 ? 4 : count > 128 ? 128 : count;
}

static void cubic_to( struct path *path, float x1, float y1, float x2, float y2, float x, float y )
{
    float x0 = path->x, y0 = path->y;
    float length = hypotf( x1 - x0, y1 - y0 ) + hypotf( x2 - x1, y2 - y1 ) + hypotf( x - x2, y - y2 );
    int i, count = pieces( path, length );

    for (i = 1; i <= count; i++)
    {
        float t = (float)i / count, u = 1 - t;
        float a = u * u * u, b = 3 * u * u * t, c = 3 * u * t * t, e = t * t * t;

        if (i == count) line_to( path, x, y );
        else line_to( path, a * x0 + b * x1 + c * x2 + e * x, a * y0 + b * y1 + c * y2 + e * y );
    }
}

static void quadratic_to( struct path *path, float x1, float y1, float x, float y )
{
    float x0 = path->x, y0 = path->y;

    cubic_to( path, x0 + 2.0f / 3 * (x1 - x0), y0 + 2.0f / 3 * (y1 - y0),
              x + 2.0f / 3 * (x1 - x), y + 2.0f / 3 * (y1 - y), x, y );
}

static float angle_between( float ux, float uy, float vx, float vy )
{
    return atan2f( ux * vy - uy * vx, ux * vx + uy * vy );
}

/* An elliptical arc from its end points, converted to its centre as SVG's
 * implementation notes describe (F.6.5 and F.6.6). */
static void arc_to( struct path *path, float rx, float ry, float rotation, int large, int sweep, float x, float y )
{
    float x1 = path->x, y1 = path->y, phi = rotation * (float)M_PI / 180, c = cosf( phi ), s = sinf( phi );
    float dx = (x1 - x) / 2, dy = (y1 - y) / 2, x1p = c * dx + s * dy, y1p = -s * dx + c * dy;
    float lambda, numerator, denominator, factor, cxp, cyp, cx, cy, ux, uy, theta, delta;
    int i, count;

    if (x1 == x && y1 == y) return;
    rx = fabsf( rx );
    ry = fabsf( ry );
    if (!rx || !ry)
    {
        line_to( path, x, y );
        return;
    }
    if ((lambda = x1p * x1p / (rx * rx) + y1p * y1p / (ry * ry)) > 1)
    {
        rx *= sqrtf( lambda );
        ry *= sqrtf( lambda );
    }
    numerator = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p;
    denominator = rx * rx * y1p * y1p + ry * ry * x1p * x1p;
    factor = numerator > 0 && denominator > 0 ? sqrtf( numerator / denominator ) : 0;
    if (large == sweep) factor = -factor;
    cxp = factor * rx * y1p / ry;
    cyp = -factor * ry * x1p / rx;
    cx = c * cxp - s * cyp + (x1 + x) / 2;
    cy = s * cxp + c * cyp + (y1 + y) / 2;
    ux = (x1p - cxp) / rx;
    uy = (y1p - cyp) / ry;
    theta = angle_between( 1, 0, ux, uy );
    delta = angle_between( ux, uy, (-x1p - cxp) / rx, (-y1p - cyp) / ry );
    if (!sweep && delta > 0) delta -= 2 * (float)M_PI;
    else if (sweep && delta < 0) delta += 2 * (float)M_PI;

    count = pieces( path, fabsf( delta ) * (rx > ry ? rx : ry) );
    for (i = 1; i <= count; i++)
    {
        float t = theta + delta * i / count;

        if (i == count) line_to( path, x, y );
        else line_to( path, cx + rx * cosf( t ) * c - ry * sinf( t ) * s, cy + rx * cosf( t ) * s + ry * sinf( t ) * c );
    }
}

static const char *skip_separators( const char *p )
{
    while (*p == ' ' || *p == ',' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

static int is_command( char c )
{
    return strchr( "MmLlHhVvCcSsQqTtAaZz", c ) && c;
}

static int read_numbers( const char **p, float *out, int count )
{
    int i;

    for (i = 0; i < count; i++)
    {
        char *end;

        *p = skip_separators( *p );
        out[i] = strtof( *p, &end );
        if (end == *p) return 0;
        *p = end;
    }
    return 1;
}

/* An arc's flags are single digits and may run into the next number: "0 1 10 20" or "0110 20". */
static int read_flag( const char **p, int *flag )
{
    *p = skip_separators( *p );
    if (**p != '0' && **p != '1') return 0;
    *flag = *(*p)++ == '1';
    return 1;
}

static int parse( struct path *path, const char *p )
{
    float v[7], last_cx = 0, last_cy = 0;
    char command = 0, previous = 0;

    for (;;)
    {
        int relative;
        float ox, oy;

        p = skip_separators( p );
        if (!*p) break;
        if (is_command( *p )) command = *p++;
        else if (!command || command == 'Z' || command == 'z') return 0;

        relative = command >= 'a';
        ox = relative ? path->x : 0;
        oy = relative ? path->y : 0;
        switch (command)
        {
        case 'Z': case 'z':
            close_path( path );
            path->x = path->start_x;
            path->y = path->start_y;
            break;
        case 'M': case 'm':
            if (!read_numbers( &p, v, 2 )) return 0;
            close_path( path );
            path->x = path->start_x = ox + v[0];
            path->y = path->start_y = oy + v[1];
            /* More pairs after a move draw lines. */
            command = relative ? 'l' : 'L';
            break;
        case 'L': case 'l':
            if (!read_numbers( &p, v, 2 )) return 0;
            line_to( path, ox + v[0], oy + v[1] );
            break;
        case 'H': case 'h':
            if (!read_numbers( &p, v, 1 )) return 0;
            line_to( path, ox + v[0], path->y );
            break;
        case 'V': case 'v':
            if (!read_numbers( &p, v, 1 )) return 0;
            line_to( path, path->x, oy + v[0] );
            break;
        case 'C': case 'c':
            if (!read_numbers( &p, v, 6 )) return 0;
            last_cx = ox + v[2];
            last_cy = oy + v[3];
            cubic_to( path, ox + v[0], oy + v[1], last_cx, last_cy, ox + v[4], oy + v[5] );
            break;
        case 'S': case 's':
        {
            int follows = previous == 'C' || previous == 'c' || previous == 'S' || previous == 's';
            float x1 = follows ? 2 * path->x - last_cx : path->x, y1 = follows ? 2 * path->y - last_cy : path->y;

            if (!read_numbers( &p, v, 4 )) return 0;
            last_cx = ox + v[0];
            last_cy = oy + v[1];
            cubic_to( path, x1, y1, last_cx, last_cy, ox + v[2], oy + v[3] );
            break;
        }
        case 'Q': case 'q':
            if (!read_numbers( &p, v, 4 )) return 0;
            last_cx = ox + v[0];
            last_cy = oy + v[1];
            quadratic_to( path, last_cx, last_cy, ox + v[2], oy + v[3] );
            break;
        case 'T': case 't':
        {
            int follows = previous == 'Q' || previous == 'q' || previous == 'T' || previous == 't';

            if (!read_numbers( &p, v, 2 )) return 0;
            last_cx = follows ? 2 * path->x - last_cx : path->x;
            last_cy = follows ? 2 * path->y - last_cy : path->y;
            quadratic_to( path, last_cx, last_cy, ox + v[0], oy + v[1] );
            break;
        }
        case 'A': case 'a':
        {
            int large, sweep;

            if (!read_numbers( &p, v, 3 ) || !read_flag( &p, &large ) || !read_flag( &p, &sweep ) ||
                !read_numbers( &p, v + 3, 2 )) return 0;
            arc_to( path, v[0], v[1], v[2], large, sweep, ox + v[3], oy + v[4] );
            break;
        }
        }
        previous = command;
        if (path->failed) return 0;
    }
    close_path( path );
    return !path->failed;
}

static int compare_crossings( const void *a, const void *b )
{
    float x = *(const float *)a, y = *(const float *)b;

    return x < y ? -1 : x > y;
}

int svg_path_mask( const char *d, float view_x, float view_y, float view_w, float view_h,
                   int width, int height, unsigned char *mask )
{
    struct path path = {0};
    float *crossings = NULL;
    unsigned short *coverage = NULL;
    int sy, ok = 0;

    memset( mask, 0, (size_t)width * height );
    if (width <= 0 || height <= 0 || view_w <= 0 || view_h <= 0) return 0;
    path.scale = SAMPLES * (width / view_w < height / view_h ? width / view_w : height / view_h);
    path.offset_x = (width * SAMPLES - view_w * path.scale) / 2 - view_x * path.scale;
    path.offset_y = (height * SAMPLES - view_h * path.scale) / 2 - view_y * path.scale;
    if (!parse( &path, d )) goto done;

    /* A crossing is its x with the winding direction in the sign of a second slot. */
    if (!(crossings = malloc( (path.count ? path.count : 1) * 2 * sizeof(*crossings) )) ||
        !(coverage = calloc( width, sizeof(*coverage) ))) goto done;

    for (sy = 0; sy < height * SAMPLES; sy++)
    {
        float sample_y = sy + 0.5f;
        int i, count = 0, winding = 0;
        float span = 0;

        for (i = 0; i < path.count; i++)
        {
            const struct edge *e = &path.edges[i];
            float top = e->y0 < e->y1 ? e->y0 : e->y1, bottom = e->y0 < e->y1 ? e->y1 : e->y0;

            if (sample_y < top || sample_y >= bottom) continue;
            crossings[2 * count] = e->x0 + (sample_y - e->y0) * (e->x1 - e->x0) / (e->y1 - e->y0);
            crossings[2 * count + 1] = e->y1 > e->y0 ? 1 : -1;
            count++;
        }
        qsort( crossings, count, 2 * sizeof(*crossings), compare_crossings );
        for (i = 0; i < count; i++)
        {
            int before = winding;

            winding += (int)crossings[2 * i + 1];
            if (!before && winding) span = crossings[2 * i];
            else if (before && !winding)
            {
                /* Samples whose centres lie inside [span, end). */
                int first = (int)ceilf( span - 0.5f ), last = (int)ceilf( crossings[2 * i] - 0.5f ), sx;

                if (first < 0) first = 0;
                if (last > width * SAMPLES) last = width * SAMPLES;
                for (sx = first; sx < last; sx++) coverage[sx / SAMPLES]++;
            }
        }
        if (sy % SAMPLES == SAMPLES - 1)
        {
            unsigned char *row = mask + (size_t)(sy / SAMPLES) * width;

            for (i = 0; i < width; i++) row[i] = coverage[i] * 255 / (SAMPLES * SAMPLES);
            memset( coverage, 0, width * sizeof(*coverage) );
        }
    }
    ok = 1;

done:
    free( coverage );
    free( crossings );
    free( path.edges );
    return ok;
}
