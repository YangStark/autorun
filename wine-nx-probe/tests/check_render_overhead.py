#!/usr/bin/env python3
"""Exercise production D3D9 fast paths and GL flush addressing with host stubs.

The real functions run under ASan/UBSan; stubs only replace driver/API calls.
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]

def function(source, name):
    start = source.index(name + '(') if name + '(' in source else source.index(name + ' (')
    start = source.rfind('\n', 0, start) + 1
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'

def run(name, source):
    with tempfile.TemporaryDirectory(prefix='wine-nx-render-') as tmp:
        src, exe = Path(tmp) / 'test.c', Path(tmp) / 'test'
        src.write_text(source)
        subprocess.run(['cc', '-std=c11', '-fsanitize=address,undefined', '-g', str(src), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
    print('PASS:', name)

common = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define TRACE(...) ((void)0)
#define WARN(...) ((void)0)
#define ERR(...) ((void)0)
#define WINAPI
#define DECLSPEC_HOTPATCH
#define TRUE 1
#define FALSE 0
'''
s = (root / 'dlls/d3d9/device.c').read_text()
tss = s[s.index('static const enum wined3d_texture_stage_state tss_lookup[]'):]
tss = tss[:tss.index('};') + 2]
enums = list(dict.fromkeys(re.findall(r'WINED3D_TSS_\w+', tss)))
prefix = common + 'enum wined3d_texture_stage_state { ' + ','.join(enums) + ' };\n' + r'''
typedef unsigned DWORD, D3DRENDERSTATETYPE, D3DSAMPLERSTATETYPE, D3DTEXTURESTAGESTATETYPE;
typedef int HRESULT;
#define D3D_OK 0
#define D3DRS_POINTSIZE 154
#define D3D9_RESZ_CODE 0x7fa05000u
#define D3DVERTEXTEXTURESAMPLER0 257
#define D3DVERTEXTEXTURESAMPLER3 260
#define WINED3D_VERTEX_SAMPLER_OFFSET 16
struct state { unsigned rs[256], sampler_states[20][14], texture_states[8][64]; };
struct d3d9_device { int multithreaded, recording; struct state *stateblock_state, *update_state; };
typedef struct d3d9_device IDirect3DDevice9Ex;
#define impl_from_IDirect3DDevice9Ex(d) (d)
#define wined3d_render_state_from_d3d(s) (s)
#define wined3d_sampler_state_from_d3d(s) (s)
static int locks, held, sets, resolves;
static void wined3d_mutex_lock(void) { assert(!held); held=1; locks++; }
static void wined3d_mutex_unlock(void) { assert(held); held=0; }
static void resolve_depth_buffer(struct d3d9_device *d) { assert(held); resolves++; }
static void wined3d_stateblock_set_render_state(struct state *s, unsigned k, unsigned v)
{ assert(held); sets++; if(k<256) s->rs[k]=v; }
static void wined3d_stateblock_set_sampler_state(struct state *s,unsigned i,unsigned k,unsigned v)
{ assert(held); sets++; if(i<20 && k<14) s->sampler_states[i][k]=v; }
static void wined3d_stateblock_set_texture_stage_state(struct state *s,unsigned i,unsigned k,unsigned v)
{ assert(held); sets++; if(i<8 && k<64) s->texture_states[i][k]=v; }
'''
code = '\n'.join(function(s, name) for name in ('d3d9_device_SetRenderState', 'd3d9_device_SetTextureStageState', 'd3d9_device_SetSamplerState'))
test = r'''
int main(void) {
    struct state primary={0}, recorded={0};
    struct d3d9_device d={0,0,&primary,&primary};
    for (int i=0;i<1000;i++) {
        d3d9_device_SetRenderState(&d,7,0);
        d3d9_device_SetTextureStageState(&d,0,1,0);
        d3d9_device_SetSamplerState(&d,0,1,0);
    }
    assert(locks==0 && sets==0);
    d3d9_device_SetRenderState(&d,7,1);
    d3d9_device_SetTextureStageState(&d,2,32,9);
    d3d9_device_SetSamplerState(&d,257,1,3);
    assert(locks==3 && primary.rs[7]==1 && primary.texture_states[2][WINED3D_TSS_CONSTANT]==9);
    assert(primary.sampler_states[16][1]==3);
    d3d9_device_SetTextureStageState(&d,2,32,9);
    d3d9_device_SetSamplerState(&d,257,1,3);
    assert(locks==3);
    d.recording=1; d.update_state=&recorded;
    d3d9_device_SetRenderState(&d,7,1);
    d3d9_device_SetTextureStageState(&d,2,32,9);
    d3d9_device_SetSamplerState(&d,257,1,3);
    assert(locks==6 && recorded.rs[7]==1 && recorded.sampler_states[16][1]==3);
    assert(recorded.texture_states[2][WINED3D_TSS_CONSTANT]==9);
    d.recording=0; d.update_state=&primary; d.multithreaded=1;
    d3d9_device_SetRenderState(&d,7,1);
    d3d9_device_SetTextureStageState(&d,2,32,9);
    d3d9_device_SetSamplerState(&d,257,1,3);
    assert(locks==9);
    d.multithreaded=0;
    d3d9_device_SetRenderState(&d,D3DRS_POINTSIZE,D3D9_RESZ_CODE);
    d3d9_device_SetRenderState(&d,D3DRS_POINTSIZE,D3D9_RESZ_CODE);
    assert(resolves==2 && locks==11);
    d3d9_device_SetRenderState(&d,~0u,0);
    d3d9_device_SetSamplerState(&d,~0u,1,0);
    d3d9_device_SetSamplerState(&d,0,~0u,0);
    d3d9_device_SetTextureStageState(&d,~0u,1,0);
    d3d9_device_SetTextureStageState(&d,0,~0u,0);
    assert(!held);
}
'''
run('D3D9 redundant states, changes, recording, multithreaded devices, RESZ, invalid indices', prefix+tss+code+test)

s = (root/'dlls/wined3d/context_gl.c').read_text()
prefix = common + r'''
#define ARB_MAP_BUFFER_RANGE 0
#define APPLE_FLUSH_BUFFER_RANGE 1
#define GL_EXTCALL(x) x
#define checkGLcall(x) ((void)0)
struct wined3d_gl_info { int supported[2]; };
struct wined3d_context_gl { struct wined3d_gl_info *gl_info; };
struct wined3d_bo { int coherent; size_t buffer_offset; };
struct wined3d_bo_gl { struct wined3d_bo b; unsigned binding, id; };
struct wined3d_const_bo_address { struct wined3d_bo *buffer_object; const void *addr; };
struct wined3d_range { size_t offset,size; };
#define wined3d_bo_gl(b) ((struct wined3d_bo_gl *)(b))
static size_t offset_seen, size_seen; static unsigned calls;
static void wined3d_context_gl_bind_bo(struct wined3d_context_gl *c,unsigned b,unsigned id) {}
static void glFlushMappedBufferRange(unsigned b,size_t offset,size_t size)
{ calls++; offset_seen=offset; size_seen=size; }
#define glFlushMappedBufferRangeAPPLE glFlushMappedBufferRange
'''
code = function(s,'flush_bo_ranges') + function(s,'wined3d_context_gl_flush_bo_address')
test = r'''
int main(void) {
    struct wined3d_gl_info info={{1,0}};
    struct wined3d_context_gl ctx={&info};
    struct wined3d_bo_gl bo={{0,4096},1,2};
    struct wined3d_const_bo_address a={&bo.b,(void *)123};
    wined3d_context_gl_flush_bo_address(&ctx,&a,37);
    assert(calls==1 && offset_seen==4219 && size_seen==37);
    struct wined3d_range r={19,43};
    flush_bo_ranges(&ctx,&a,1,&r);
    assert(calls==2 && offset_seen==4238 && size_seen==43);
    bo.b.coherent=1;
    wined3d_context_gl_flush_bo_address(&ctx,&a,37);
    a.buffer_object=NULL;
    wined3d_context_gl_flush_bo_address(&ctx,&a,37);
    assert(calls==2);
}
'''
run('GL flush offsets include the suballocation and mapping offset exactly once',prefix+code+test)

s = (root/'dlls/opengl32/unix_wgl.c').read_text()
prefix = common + r'''
#define __SWITCH__ 1
#define GL_MAP_FLUSH_EXPLICIT_BIT 16
#define GL_INVALID_OPERATION 1
#define GL_INVALID_VALUE 2
#define VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE 3
typedef int TEB, VkResult;
typedef struct { int sType; void *memory; size_t offset,size; } VkMappedMemoryRange;
struct vk_device { void *vk_device; int (*p_vkFlushMappedMemoryRanges)(void *,unsigned,void *); };
struct buffer {
    void *vk_memory, *map_ptr, *host_ptr;
    struct vk_device *vk_device;
    int pinned, explicit_flush;
    size_t map_length;
    unsigned map_access;
};
static unsigned wine_nx_gl_explicit_flushes;
static int error;
static void *clean_ptr; static size_t clean_size;
static void set_gl_error(TEB *teb, int e) { error=e; }
static void wine_nx_nouveau_cpu_clean_range(void *p,size_t n) { clean_ptr=p; clean_size=n; }
'''
test = r'''
int main(void) {
    char bytes[256];
    struct buffer b={.pinned=1,.explicit_flush=1,.map_ptr=bytes+64,.map_length=128,
                     .map_access=GL_MAP_FLUSH_EXPLICIT_BIT};
    flush_buffer(NULL,&b,17,33);
    assert(clean_ptr==bytes+81 && clean_size==33 && !error && wine_nx_gl_explicit_flushes==1);
    flush_buffer(NULL,&b,128,0); assert(!error && wine_nx_gl_explicit_flushes==1);
    flush_buffer(NULL,&b,129,0); assert(error==GL_INVALID_VALUE);
    error=0; flush_buffer(NULL,&b,120,9); assert(error==GL_INVALID_VALUE);
    error=0; flush_buffer(NULL,&b,(size_t)-1,4); assert(error==GL_INVALID_VALUE);
    assert(wine_nx_gl_explicit_flushes==1);
    b.map_access=0; error=0;
    flush_buffer(NULL,&b,0,4); assert(error==GL_INVALID_OPERATION);
    b.map_access=GL_MAP_FLUSH_EXPLICIT_BIT; b.map_ptr=NULL; error=0;
    flush_buffer(NULL,&b,0,4); assert(error==GL_INVALID_OPERATION);
    b.map_ptr=bytes; b.explicit_flush=0; error=0;
    flush_buffer(NULL,&b,0,4); assert(!error && wine_nx_gl_explicit_flushes==1);
}
'''
run('Pinned flush mapping offsets, bounds, overflow, unmapped and conservative fallback',prefix+function(s,'flush_buffer')+test)

s = (root/'wine-nx-probe/build-switch-mesa/libdrm_nouveau/source/nouveau.c').read_text()
prefix = common + r'''
typedef int Mutex;
static int locked;
static void mutexLock(Mutex *m) { assert(!locked); locked=1; }
static void mutexUnlock(Mutex *m) { assert(locked); locked=0; }
struct nouveau_bo { uint64_t size; };
struct nouveau_bo_priv { struct nouveau_bo base; void *map_addr; int cacheable,explicit_flush; struct nouveau_bo_priv *pin_next; };
#define nouveau_bo(p) ((struct nouveau_bo_priv *)(p))
static Mutex nouveau_pin_lock;
static struct nouveau_bo_priv *nouveau_pins;
static unsigned wine_nx_nouveau_cache_cleans;
static uint64_t wine_nx_nouveau_cache_clean_ns, wine_nx_nouveau_cache_clean_bytes;
static int wine_nx_nouveau_skip_clean;
static uint64_t armGetSystemTick(void) { static uint64_t t; return ++t; }
static uint64_t armTicksToNs(uint64_t t) { return t; }
static void *last_ptr; static size_t last_size;
static void armDCacheClean(void *p,size_t n) { last_ptr=p;last_size=n; }
'''
# These declarations are on a separate line in libdrm.
code='void\n'+function(s,'wine_nx_nouveau_cpu_clean_range')
code+='int\n'+function(s,'wine_nx_nouveau_bo_set_explicit_flush')
code+='void\n'+function(s,'nouveau_bo_cpu_clean')
test=r'''
int main(void) {
    char bytes[4096];
    struct nouveau_bo_priv pin={.base={4096},.map_addr=bytes,.cacheable=1};
    nouveau_pins=&pin;
    nouveau_bo_cpu_clean(&pin.base);
    assert(wine_nx_nouveau_cache_cleans==1 && last_size==4096);
    assert(wine_nx_nouveau_bo_set_explicit_flush(bytes,1));
    assert(wine_nx_nouveau_cache_cleans==2); /* Initial contents published once. */
    for(int i=0;i<1000;i++) nouveau_bo_cpu_clean(&pin.base);
    assert(wine_nx_nouveau_cache_cleans==2);
    wine_nx_nouveau_cpu_clean_range(bytes+123,27);
    assert(wine_nx_nouveau_cache_cleans==3 && last_ptr==bytes+123 && last_size==27);
    assert(wine_nx_nouveau_cache_clean_bytes==8192+27);
    assert(!wine_nx_nouveau_bo_set_explicit_flush(bytes+1,1));
    assert(wine_nx_nouveau_bo_set_explicit_flush(bytes,0));
    nouveau_bo_cpu_clean(&pin.base);
    assert(wine_nx_nouveau_cache_cleans==4 && last_size==4096);
    pin.cacheable=0; nouveau_bo_cpu_clean(&pin.base);
    assert(wine_nx_nouveau_cache_cleans==4);
    wine_nx_nouveau_skip_clean=1;
    wine_nx_nouveau_cpu_clean_range(bytes,128);
    assert(wine_nx_nouveau_cache_cleans==4 && !locked);
}
'''
run('Driver initial publication, repeated submissions, exact range clean and coherent fallback',prefix+code+test)

s=(root/'wine-nx-probe/build-switch-mesa/src/src/gallium/drivers/nouveau/nouveau_buffer.c').read_text()
prefix=common+r'''
#define PIPE_TRANSFER_WRITE 2
#define NOUVEAU_BUFFER_STATUS_PINNED 64
struct pipe_box { unsigned x,width; };
struct nouveau_bo { void *map; };
struct nv04_resource { unsigned status,offset; struct nouveau_bo *bo; };
struct pipe_transfer { struct nv04_resource *resource; struct pipe_box box; unsigned usage; };
struct nouveau_transfer { struct pipe_transfer base; void *map; };
#define nv04_resource(p) (p)
static void *last_ptr; static size_t last_size; static unsigned calls;
static void wine_nx_nouveau_cpu_clean_range(void *p,size_t n) { calls++;last_ptr=p;last_size=n; }
'''
test=r'''
int main(void) {
    char bytes[4096];
    struct nouveau_bo bo={bytes};
    struct nv04_resource res={NOUVEAU_BUFFER_STATUS_PINNED,128,&bo};
    struct nouveau_transfer tx={{&res,{256,512},PIPE_TRANSFER_WRITE},NULL};
    nouveau_buffer_flush_pinned(&tx,17,33);
    assert(calls==1 && last_ptr==bytes+401 && last_size==33);
    nouveau_buffer_flush_pinned(&tx,0,512);
    assert(calls==2 && last_ptr==bytes+384 && last_size==512);
    tx.map=bytes; nouveau_buffer_flush_pinned(&tx,0,512); assert(calls==2);
    tx.map=NULL; tx.base.usage=0;
    nouveau_buffer_flush_pinned(&tx,0,512); assert(calls==2);
    tx.base.usage=PIPE_TRANSFER_WRITE; res.status=0;
    nouveau_buffer_flush_pinned(&tx,0,512); assert(calls==2);
}
'''
run('Mesa CPU transfers clean pinned writes, preserving staging, read-only and ordinary buffers',prefix+'static void\n'+function(s,'nouveau_buffer_flush_pinned')+test)
