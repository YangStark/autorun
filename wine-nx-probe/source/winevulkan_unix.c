/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later.
 * winevulkan's unix side (dlls/winevulkan/vulkan.c and vulkan_thunks.c) is linked
 * into runtimes built with mesa-switch, with its tables renamed; the x86 unix
 * call gate calls the WoW64 table only below this size. */
#include "../../dlls/winevulkan/vulkan_loader.h"

const unsigned int wine_nx_winevulkan_wow64_unix_count = unix_count;
