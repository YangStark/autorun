/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later.
 * Declarations needed by Box64's shared headers. The execution-only target
 * does not link or implement the Linux mmap layer. */
#ifndef WINE_NX_BOX64_MMAN_H
#define WINE_NX_BOX64_MMAN_H
#include <stddef.h>
#include <sys/types.h>
#define PROT_NONE 0
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4
#define MAP_SHARED 1
#define MAP_PRIVATE 2
#define MAP_FIXED 0x10
#define MAP_ANONYMOUS 0x20
#define MAP_32BIT 0x40
#define MAP_NORESERVE 0x4000
#define MAP_FAILED ((void *)-1)
void *mmap(void *, size_t, int, int, int, off_t);
int munmap(void *, size_t);
int mprotect(void *, size_t, int);
#endif
