/* Portable check for the default and build-selected Wine-NX SD root. */
#include <assert.h>
#include <string.h>

#include "../../include/wine/nx_root.h"

#ifndef WINE_NX_TEST_EXPECTED_ROOT
#define WINE_NX_TEST_EXPECTED_ROOT "/switch/wine"
#endif

int main(void)
{
    assert(!strcmp(WINE_NX_ROOT, WINE_NX_TEST_EXPECTED_ROOT));
    assert(!strcmp(WINE_NX_SD_ROOT, "sdmc:" WINE_NX_TEST_EXPECTED_ROOT));
    return 0;
}
