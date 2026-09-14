/* Host test for a program's Box64 options file lines (source/box64_options.h). */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../source/box64_options.h"

int main(void)
{
    char name[32];
    long value;

    assert( nx_box64_option_line( "BOX64_DYNAREC_SAFEFLAGS=0\n", name, sizeof(name), &value ) );
    assert( !strcmp( name, "BOX64_DYNAREC_SAFEFLAGS" ) && value == 0 );
    /* Spaces around both sides, a trailing comment and CRLF. */
    assert( nx_box64_option_line( "  BOX64_DYNAREC_FORWARD = 1024  # longer jumps\r\n", name, sizeof(name), &value ) );
    assert( !strcmp( name, "BOX64_DYNAREC_FORWARD" ) && value == 1024 );
    assert( nx_box64_option_line( "BOX64_DYNAREC_BIGBLOCK=0x3", name, sizeof(name), &value ) && value == 3 );
    /* Nothing to apply: comments, blank lines, no value, junk after it, no name, a name too long. */
    assert( !nx_box64_option_line( "# BOX64_DYNAREC_BIGBLOCK=3\n", name, sizeof(name), &value ) );
    assert( !nx_box64_option_line( "   \n", name, sizeof(name), &value ) );
    assert( !nx_box64_option_line( "BOX64_DYNAREC_BIGBLOCK=\n", name, sizeof(name), &value ) );
    assert( !nx_box64_option_line( "BOX64_DYNAREC_BIGBLOCK=3 fast\n", name, sizeof(name), &value ) );
    assert( !nx_box64_option_line( "=3\n", name, sizeof(name), &value ) );
    assert( !nx_box64_option_line( "BOX64_DYNAREC_ALIGNED_ATOMICS_AND_MORE=1\n", name, sizeof(name), &value ) );
    assert( !nx_box64_option_line( "BOX64_DYNAREC_BIGBLOCK 3\n", name, sizeof(name), &value ) );

    puts( "Box64 options file: names, values, spaces, comments and refusals passed" );
    return 0;
}
