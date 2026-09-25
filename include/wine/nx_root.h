#ifndef WINE_NX_ROOT_H
#define WINE_NX_ROOT_H

/* POSIX path within the SD card; CMake sets this for a self-contained game. */
#ifndef WINE_NX_ROOT
#define WINE_NX_ROOT "/switch/wine"
#endif
#define WINE_NX_SD_ROOT "sdmc:" WINE_NX_ROOT

#endif /* WINE_NX_ROOT_H */
