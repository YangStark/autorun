#!/usr/bin/env python3
"""Run libdrm_nouveau's buffer object cache (nouveau_bo_cache_take and
nouveau_bo_cache_put in source/nouveau.c of the Wine-NX fork) on the case that
kept WarCraft III's texture uploads allocating: a cache filled with sizes no one
asks for again, then uploads freeing and asking for their own. WINE_NX_LIBDRM_SRC
names the fork, as for build-switch-mesa.sh (default ~/libdrm_nouveau)."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

src = Path(os.environ.get('WINE_NX_LIBDRM_SRC', Path.home() / 'libdrm_nouveau'))
nouveau = (src / 'source/nouveau.c').read_text()

def function(source, name):
    start = source.index(name)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

defines = '\n'.join(re.findall(r'^#define NOUVEAU_BO_CACHE_\w+.*$', nouveau, re.M))

fixture = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef int Mutex;
typedef int NvKind;
static int locked;
static void mutexLock(Mutex *m) { assert(!locked); locked = 1; }
static void mutexUnlock(Mutex *m) { assert(locked); locked = 0; }

struct nouveau_device { int unused; };
struct nouveau_bo { struct nouveau_device *device; uint64_t size; uint32_t flags; void *map; };
struct nouveau_bo_priv { struct nouveau_bo base; NvKind kind; uint32_t align; int user_memory; int destroyed; };

static struct nouveau_bo_priv *destroyed[256];
static unsigned int destroyed_count;
static void nouveau_bo_destroy(struct nouveau_bo_priv *nvbo)
{
    assert(!locked);
    nvbo->destroyed = 1;
    destroyed[destroyed_count++] = nvbo;
}

static Mutex nouveau_bo_cache_lock;
static struct nouveau_bo_priv *nouveau_bo_cache[NOUVEAU_BO_CACHE_ENTRIES];
static unsigned int nouveau_bo_cache_count;
static uint64_t nouveau_bo_cache_bytes;
unsigned int wine_nx_nouveau_bo_new, wine_nx_nouveau_bo_reused, wine_nx_nouveau_bo_evicted;
'''

tests = r'''
#define MiB (1ull << 20)
#define GART 2
static struct nouveau_device device;
static struct nouveau_bo_priv pool[512];
static unsigned int pool_used;

static struct nouveau_bo_priv *make(uint64_t size, NvKind kind)
{
    struct nouveau_bo_priv *nvbo = &pool[pool_used++];

    memset(nvbo, 0, sizeof(*nvbo));
    nvbo->base.device = &device;
    nvbo->base.size = size;
    nvbo->base.flags = GART;
    nvbo->kind = kind;
    nvbo->align = 0x1000;
    return nvbo;
}

static void check_consistent(void)
{
    uint64_t bytes = 0;
    unsigned int i;

    for (i = 0; i < nouveau_bo_cache_count; i++) bytes += nouveau_bo_cache[i]->base.size;
    assert(bytes == nouveau_bo_cache_bytes);
    assert(nouveau_bo_cache_count <= NOUVEAU_BO_CACHE_ENTRIES && bytes <= NOUVEAU_BO_CACHE_BYTES);
}

int main(void)
{
    struct nouveau_bo_priv *stale[NOUVEAU_BO_CACHE_ENTRIES], *upload[32], *taken;
    unsigned int i, frame, reused = 0;

    /* A loaded map leaves tiled textures of sizes never asked for again. */
    for (i = 0; i < NOUVEAU_BO_CACHE_ENTRIES; i++)
    {
        stale[i] = make(0x3000 + i * 0x1000, 0xfe);
        assert(nouveau_bo_cache_put(stale[i]));
    }
    check_consistent();
    assert(nouveau_bo_cache_count == NOUVEAU_BO_CACHE_ENTRIES && !destroyed_count);

    /* Then every frame frees 24 staging bos of three sizes and asks for them again. */
    for (frame = 0; frame < 10; frame++)
    {
        for (i = 0; i < 24; i++)
        {
            uint64_t size = (i % 3 + 1) * 0x10000;

            if ((taken = nouveau_bo_cache_take(&device, GART, 0x1000, size, 0)))
            {
                assert(taken->base.size == size && taken->kind == 0 && !taken->destroyed);
                reused++;
            }
            upload[i] = taken ? taken : make(size, 0);
        }
        for (i = 0; i < 24; i++) assert(nouveau_bo_cache_put(upload[i]));
        check_consistent();
    }
    printf("staging bos reused from the second frame on: %u of %u\n", reused, 9 * 24);
    assert(reused == 9 * 24);
    /* The stale textures made room, oldest first, and were destroyed. */
    assert(wine_nx_nouveau_bo_evicted == 24 && destroyed_count == 24);
    for (i = 0; i < 24; i++) assert(destroyed[i] == stale[i]);

    /* The newest match is taken, and what remains keeps its order. */
    {
        struct nouveau_bo_priv *a = make(0x20000, 0x10), *b = make(0x20000, 0x10);

        assert(nouveau_bo_cache_put(a) && nouveau_bo_cache_put(b));
        assert(nouveau_bo_cache_take(&device, GART, 0x1000, 0x20000, 0x10) == b);
        assert(nouveau_bo_cache_take(&device, GART, 0x1000, 0x20000, 0x10) == a);
        for (i = 1; i < nouveau_bo_cache_count; i++)
            assert(nouveau_bo_cache[i - 1] < nouveau_bo_cache[i] || nouveau_bo_cache[i]->kind == 0);
        check_consistent();
    }

    /* The byte cap makes room too: eight 8 MiB bos, then one more. */
    destroyed_count = 0;
    while (nouveau_bo_cache_count)
        nouveau_bo_cache_take(&device, nouveau_bo_cache[0]->base.flags, 0, nouveau_bo_cache[0]->base.size,
                              nouveau_bo_cache[0]->kind);
    for (i = 0; i < 8; i++) assert(nouveau_bo_cache_put(make(8 * MiB, 0x20)));
    assert(nouveau_bo_cache_bytes == NOUVEAU_BO_CACHE_BYTES);
    assert(nouveau_bo_cache_put(make(4 * MiB, 0x20)) && destroyed_count == 1);
    check_consistent();

    /* Bos over the size limit and pinned application memory stay out. */
    {
        struct nouveau_bo_priv *pinned = make(0x1000, 0);

        pinned->user_memory = 1;
        assert(!nouveau_bo_cache_put(make(NOUVEAU_BO_CACHE_MAX_BO + 0x1000, 0)));
        assert(!nouveau_bo_cache_put(pinned));
    }
    check_consistent();

    printf("PASS: libdrm_nouveau's bo cache\n");
    return 0;
}
'''

source = (defines + fixture + function(nouveau, 'static struct nouveau_bo_priv *\nnouveau_bo_cache_take') + '\n' +
          function(nouveau, 'static bool\nnouveau_bo_cache_put') + '\n' + tests)
with tempfile.TemporaryDirectory() as tmp:
    c_file = Path(tmp) / 'bo_cache.c'
    exe = Path(tmp) / 'bo_cache'
    c_file.write_text(source)
    subprocess.run(['cc', '-std=gnu99', '-Wall', '-Wno-unused-parameter', '-Werror', '-o', str(exe), str(c_file)],
                   check=True)
    subprocess.run([str(exe)], check=True)
