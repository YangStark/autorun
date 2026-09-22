# LSFG-VK for Horizon

Uses the last GPL archive revision, `8b0da2661c6f3473a7fccc8ba643880050e71642`
(June 28, 2026), from https://git.lsfg-vk.dev/lsfg-vk-archive.git.
This is the 2.0 development branch, not the later noncommercial/no-derivatives release.

Run `sh wine-nx-probe/tools/bootstrap-lsfg-vk.sh` before configuring a mesa-switch
build. CMake enables `WINE_NX_LSFG` by default for mesa-switch and requires
`glslangValidator`. Set `-DWINE_NX_LSFG=OFF` to exclude the GPL backend.
The pinned source plus `horizon.patch` builds the native library; no Linux Vulkan
layer or external-memory FD transport is used.

Copy `Lossless.dll` from your own compatible Lossless Scaling installation to
`sdmc:/switch/wine/lsfg/Lossless.dll`. It is not included or downloaded.
Enable **LSFG-VK (2x)** in the game's **Frame Generation** settings. Defaults:
disabled, performance mode on, motion resolution 25%.

The port supports SDR RGBA8/BGRA8 Vulkan swapchains, including DXVK and VKD3D.
It inserts one frame and uses FIFO presentation. Frame limits apply to real game
frames; 30 FPS targets 60 displayed FPS with 2x generation. Motion resolution
changes optical-flow processing, not the output resolution. OpenGL and HDR are
not supported. Pipeline compilation on first use can be slow; the cache is saved
under `sdmc:/switch/wine/cache/lsfg-vk.bin`.

The borrowed-device API and NVK reduction shaders are adapted from Cemu-NX's
GPL LSFG port (NaGaa95/Cemu-nx, `fcae0752`). The two large reduction passes use
separate 8x8 dispatches. Their output is not assumed bit-identical to upstream.
Presentation synchronization, color conversion and temporal warmup are handled
by Autorun. Build and host checks do not establish Switch performance or visual
correctness; those require hardware testing.

LSFG-VK is GPL-3.0-or-later. Distribute its license and corresponding source,
including these changes and build scripts, with public releases of the combined
runtime.
