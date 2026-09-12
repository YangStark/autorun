/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later.
 * opengl32's unix side (dlls/opengl32/unix_wgl.c and unix_thunks.c) is linked
 * into the runtime with its tables renamed; the x86 unix call gate calls the
 * WoW64 table only below this size. */
#include <stdarg.h>
#include <stddef.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "../../dlls/opengl32/unixlib.h"  /* not ntdll's unixlib.h, which is first in the include path */

const unsigned int wine_nx_opengl32_wow64_unix_count = funcs_count;
