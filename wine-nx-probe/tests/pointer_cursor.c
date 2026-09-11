/* Host test for the analog-stick cursor (source/pointer_cursor.h). */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../source/pointer_cursor.h"

#define MS 1000000ull

static struct pointer_cursor cursor_at( double x, double y )
{
    struct pointer_cursor c = { .width = 1280, .height = 720 };

    pointer_cursor_place( &c, x, y );
    return c;
}

static void test_dead_zone_ignores_drift(void)
{
    struct pointer_cursor c = cursor_at( 640, 360 );

    assert( !pointer_cursor_step( &c, 3000, -2500, 50 * MS ) );
    assert( !pointer_cursor_step( &c, 0, 4000, 50 * MS ) );
    assert( c.x == 640 && c.y == 360 );
}

static void test_full_tilt_speed_and_axes(void)
{
    struct pointer_cursor c = cursor_at( 100, 360 );
    int i;

    /* One second of full right tilt in 20 ms polls. */
    for (i = 0; i < 50; i++) assert( pointer_cursor_step( &c, 32767, 0, 20 * MS ) );
    assert( fabs( c.x - 1100 ) < 0.001 && c.y == 360 );
    /* Stick up is positive on libnx and moves the cursor toward row 0. */
    assert( pointer_cursor_step( &c, 0, 32767, 100 * MS / 2 ) );
    assert( fabs( c.y - 310 ) < 0.001 );
    assert( pointer_cursor_step( &c, -32767, -32767, 50 * MS ) );
    assert( c.x < 1100 && c.y > 310 );
}

static void test_diagonal_is_not_faster(void)
{
    struct pointer_cursor c = cursor_at( 400, 400 );

    /* A square gate can report both axes at maximum. */
    pointer_cursor_step( &c, 32767, 32767, 50 * MS );
    assert( fabs( hypot( c.x - 400, c.y - 400 ) - 50 ) < 0.001 );
}

static void test_quadratic_response(void)
{
    struct pointer_cursor c = cursor_at( 0, 0 );
    int half = (int)(POINTER_CURSOR_DEAD_ZONE + (POINTER_CURSOR_STICK_MAX - POINTER_CURSOR_DEAD_ZONE) / 2);

    pointer_cursor_step( &c, half, 0, 40 * MS );
    assert( fabs( c.x - 10 ) < 0.01 ); /* a quarter of 1000 px/s for 40 ms */
}

static void test_slow_tilt_accumulates_subpixels(void)
{
    struct pointer_cursor c = cursor_at( 200, 200 );
    int moved = 0, polls = 0;

    /* About 11 px/s: a pixel takes several 10 ms polls. */
    while (!moved)
    {
        moved = pointer_cursor_step( &c, 7000, 0, 10 * MS );
        polls++;
    }
    assert( polls > 3 && (int)c.x == 201 && c.y == 200 );
}

static void test_stall_and_edges(void)
{
    struct pointer_cursor c = cursor_at( 640, 360 );

    /* Two seconds without a poll still moves only one capped step. */
    pointer_cursor_step( &c, 32767, 0, 2000 * MS );
    assert( fabs( c.x - 690 ) < 0.001 );
    c = cursor_at( 1279, 0 );
    assert( !pointer_cursor_step( &c, 32767, 32767, 50 * MS ) );
    assert( c.x == 1279 && c.y == 0 );
    c = cursor_at( -20, 9999 );
    assert( c.x == 0 && c.y == 719 );
}

static void test_paint_restores_pixels(void)
{
    enum { W = 64, H = 40, STRIDE = 70 };
    static uint32_t bits[STRIDE * H], before[STRIDE * H];
    struct pointer_cursor c = { .width = W, .height = H };
    unsigned int i, black = 0, white = 0;

    for (i = 0; i < STRIDE * H; i++) bits[i] = 0xff000000u | (i * 2654435761u >> 8);
    memcpy( before, bits, sizeof(bits) );

    pointer_cursor_place( &c, 10, 5 );
    pointer_cursor_paint( &c, bits, STRIDE, 1 );
    assert( bits[5 * STRIDE + 10] == 0xff000000u );            /* the tip */
    assert( bits[7 * STRIDE + 11] == 0xffffffffu );            /* fill */
    assert( bits[5 * STRIDE + 11] == before[5 * STRIDE + 11] ); /* transparent */
    for (i = 0; i < STRIDE * H; i++)
    {
        if (bits[i] == before[i]) continue;
        if (bits[i] == 0xff000000u) black++;
        else if (bits[i] == 0xffffffffu) white++;
        else assert( 0 );
    }
    assert( black > 30 && white > 40 );
    pointer_cursor_paint( &c, bits, STRIDE, 0 );
    assert( !memcmp( bits, before, sizeof(bits) ) );

    /* At the bottom-right corner only the tip is on screen. */
    pointer_cursor_place( &c, W - 1, H - 1 );
    pointer_cursor_paint( &c, bits, STRIDE, 1 );
    assert( bits[(H - 1) * STRIDE + W - 1] == 0xff000000u );
    assert( bits[(H - 1) * STRIDE + W] == before[(H - 1) * STRIDE + W] ); /* stride padding */
    pointer_cursor_paint( &c, bits, STRIDE, 0 );
    assert( !memcmp( bits, before, sizeof(bits) ) );
}

int main(void)
{
    test_dead_zone_ignores_drift();
    test_full_tilt_speed_and_axes();
    test_diagonal_is_not_faster();
    test_quadratic_response();
    test_slow_tilt_accumulates_subpixels();
    test_stall_and_edges();
    test_paint_restores_pixels();
    puts( "pointer cursor: dead zone, speed curve, time scaling, edges and sprite restore passed" );
    return 0;
}
