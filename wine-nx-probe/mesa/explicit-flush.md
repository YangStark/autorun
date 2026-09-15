# Wine-NX rendering changes (build 100)

The runtime supplies `WINE_D3D_CONFIG=cs_spin_count=64,explicit_buffer_flush=1`.
Other Wine environments keep the previous 2,000-iteration polling limit and
coherent buffer-storage flags unless they select these options.

D3D9 ignores unchanged render, texture-stage and sampler states before entering
WineD3D's global mutex on devices created without `D3DCREATE_MULTITHREADED`.
It reads the primary state rather than maintaining another cache, so state-block
Apply and device Reset do not leave stale cached values. State-block recording,
multithreaded devices, invalid indices and the RESZ depth-resolve command retain
the normal path. Changed states still use the mutex.

## Buffer visibility

WineD3D requests noncoherent persistent storage and explicitly flushes its written
ranges. Both standalone buffers and suballocated chunks use matching storage and
mapping flags, and their `wined3d_bo.coherent` field is false so accelerated maps
also submit their upload/flush on unmap. A flush adds the BO suballocation and
mapping offsets exactly once.

The WoW64 OpenGL bridge marks these pins as explicitly flushed via libdrm. The
handshake publishes the initial contents once. Subsequent
`glFlushMappedBufferRange` calls clean only the requested CPU cache range, after
checking the mapped range. The driver skips its per-submission whole-BO clean
only for pins that completed the handshake. Coherent pins, older libraries
without the handshake, and write mappings without `GL_MAP_FLUSH_EXPLICIT_BIT`
keep the conservative submission clean. This does not disable cache maintenance.

Mesa's own direct CPU transfers (including `glBufferSubData`) clean their written
pinned ranges too. Staging transfers retain their GPU upload path. The new Mesa
and libdrm patches must be built together; `build-switch-mesa.sh` applies both
on top of the existing Wine-NX patches. The external source checkouts are not
modified by the build.

`[PROGRESS]` reports cumulative `clean_ms`, `clean_mb`, and `range_flushes`.
`clean_mb` includes the initial publication, driver transfers and automatic
fallback cleans. `range_flushes` counts nonempty explicit WoW64 flushes. Compare
deltas over the same driving scene; these counters do not measure GPU utilization.

## Validation

Build Mesa first, then run:

```
python3 wine-nx-probe/tests/check_render_overhead.py
python3 wine-nx-probe/tests/check_bo_cache.py
```

The first test compiles the production functions with host API stubs under
AddressSanitizer and UndefinedBehaviorSanitizer. It checks redundant and changed
states, recording, multithreaded access, RESZ, invalid indices, suballocation and
mapping offsets, out-of-range flushes, initial publication, repeated submissions,
coherent fallback and Mesa direct/staged/read-only transfers.

The i386 D3D9/WineD3D DLLs and the ARM64 runtime must be rebuilt together. Build 100
was cross-built successfully. Switch gameplay, rendering correctness and FPS
remain hardware validation; host tests cannot verify Tegra cache coherence or
scheduling latency.
