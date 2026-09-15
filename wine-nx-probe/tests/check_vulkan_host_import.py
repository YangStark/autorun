#!/usr/bin/env python3
"""Run Mesa's host-import policy and NvMap error paths with host API stubs.

Usage: check_vulkan_host_import.py [path/to/mesa-switch]
The Switch PE test covers real CPU/GPU coherence; these tests cover policy,
cache-attribute failures and NvMap cleanup with ASan/UBSan.
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

mesa = Path(sys.argv[1]) if len(sys.argv) > 1 else Path.home() / "mesa-switch"


def function(path, name):
    source = (mesa / path).read_text()
    start = re.search(r"^" + name + r"\(", source, re.M).start()
    brace = source.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <vulkan/vulkan_core.h>
struct nvk_physical_device { uint32_t mem_type_count; VkMemoryType mem_types[4]; };
struct nvk_device { struct nvk_physical_device *pdev; };
#define VK_FROM_HANDLE(type, name, handle) struct type *name = (struct type *)(handle)
#define nvk_device_physical(dev) ((dev)->pdev)
#define vk_error(dev, result) (result)
enum nvkmd_mem_flags { NVKMD_MEM_COHERENT = 1 << 5, NVKMD_MEM_GPU_UNCACHED = 1 << 6 };
enum { NOUVEAU_HORIZON_MEMORY_CPU_VISIBLE = 1,
       NOUVEAU_HORIZON_MEMORY_CPU_CACHED = 2, NOUVEAU_HORIZON_MEMORY_GPU_CACHED = 4 };
typedef uint32_t Result, NvKind;
typedef struct { bool live, cached; } NvMap;
#define R_SUCCEEDED(rc) ((rc) == 0)
#define R_FAILED(rc) ((rc) != 0)
static Result create_result, attribute_result;
static unsigned creates, attributes, closes;
static void *expected_addr;
static Result nvMapCreate(NvMap *map, void *addr, uint32_t size,
                          uint32_t align, NvKind kind, bool cached)
{
    assert(addr == expected_addr && size == 65536 && align == 4096 && kind == 0);
    creates++;
    map->live = !create_result;
    map->cached = cached;
    return create_result;
}
static Result svcSetMemoryAttribute(void *addr, uint32_t size, unsigned mask, unsigned value)
{
    assert(addr == expected_addr && size == 65536 && mask == 8 && value == 8);
    attributes++;
    return attribute_result;
}
static void nvMapClose(NvMap *map)
{
    assert(map->live);
    map->live = false;
    closes++;
}
'''

code = "VkResult\n" + function("src/nouveau/vulkan/nvk_device_memory.c", "nvk_GetMemoryHostPointerPropertiesEXT")
code += "uint32_t\n" + function("src/nouveau/vulkan/nvkmd/switch/nvkmd_switch_dev.c", "nvkmd_switch_memory_flags")
code += "Result\n" + function("src/nouveau/horizon/nouveau_horizon_memory.c", "nouveau_horizon_memory_nvmap_create")

test = r'''
int main(void)
{
    struct nvk_physical_device pdev = { .mem_type_count = 3, .mem_types = {
        { VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, 0 },
        { VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0 },
        { VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0 },
    }};
    struct nvk_device dev = { &pdev };
    VkMemoryHostPointerPropertiesEXT props = { .sType = VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT };
    char page;
    expected_addr = &page;
    VkResult res = nvk_GetMemoryHostPointerPropertiesEXT((VkDevice)&dev,
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT, &page, &props);
    assert(res == VK_SUCCESS);
#ifdef __SWITCH__
    assert(props.memoryTypeBits == 3); /* cached AND coherent, never device-only */
#else
    assert(props.memoryTypeBits == 1); /* preserve non-Switch policy */
#endif
    assert(nvk_GetMemoryHostPointerPropertiesEXT((VkDevice)&dev,
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT, &page, &props) == VK_ERROR_INVALID_EXTERNAL_HANDLE);
    pdev.mem_type_count = 0;
    assert(nvk_GetMemoryHostPointerPropertiesEXT((VkDevice)&dev,
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT, &page, &props) == VK_ERROR_INVALID_EXTERNAL_HANDLE);

    NvMap map = {0};
    /* Exercise the cache policy through to the actual NvMap wrapper. */
    for (unsigned coherent = 0; coherent < 2; coherent++) {
        for (unsigned gpu_uncached = 0; gpu_uncached < 2; gpu_uncached++) {
            unsigned flags = nvkmd_switch_memory_flags((coherent ? NVKMD_MEM_COHERENT : 0) |
                                                       (gpu_uncached ? NVKMD_MEM_GPU_UNCACHED : 0));
            assert(flags & NOUVEAU_HORIZON_MEMORY_CPU_VISIBLE);
            assert(!!(flags & NOUVEAU_HORIZON_MEMORY_GPU_CACHED) == !gpu_uncached);
            attributes = 0;
            assert(nouveau_horizon_memory_nvmap_create(&map, &page, 65536, 4096, 0,
                flags & NOUVEAU_HORIZON_MEMORY_CPU_CACHED) == 0);
            assert(map.live && map.cached == !coherent && attributes == coherent);
            nvMapClose(&map);
        }
    }
    /* An ignored libnx attribute failure must NOT become successful coherent memory. */
    attribute_result = 123;
    attributes = closes = 0;
    assert(nouveau_horizon_memory_nvmap_create(&map, &page, 65536, 4096, 0, false) == 123);
    assert(attributes == 1 && closes == 1 && !map.live);
    /* nvMapCreate owns cleanup on its own failure: no second close or attribute call. */
    create_result = 456;
    attributes = closes = 0;
    assert(nouveau_horizon_memory_nvmap_create(&map, &page, 65536, 4096, 0, false) == 456);
    assert(attributes == 0 && closes == 0 && !map.live);
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="wine-nx-vulkan-") as tmp:
    src, exe = Path(tmp) / "test.c", Path(tmp) / "test"
    src.write_text(prefix + code + test)
    for defines in (["-D__SWITCH__"], []):
        subprocess.run(["cc", "-std=c11", "-fsanitize=address,undefined", "-g",
                        "-I", str(mesa / "include"), *defines, str(src), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
        print("PASS:", "Switch" if defines else "non-Switch", "import policy, cache mode, failure cleanup")
