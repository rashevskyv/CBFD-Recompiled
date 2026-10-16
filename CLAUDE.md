# Conker's Bad Fur Day: Recompiled → Nintendo Switch — notes for Claude

Goal: run this N64 static recompilation (N64Recomp + N64ModernRuntime + RT64 +
RecompFrontend) natively on Nintendo Switch as a libnx NRO. This is the fork
`rashevskyv/CBFD-Recompiled`, branch `switch` (default). Upstream is
`sciaschi/CBFD-Recompiled` (`master`); merge upstream into `switch`, never the reverse.

Read `README.md` (build on Linux) and `CONTRIBUTING.md` first. Write every finding
into the "Findings" section below: symptom, cause, file, fix, how found, date.

## What is already known

- The game logic is already C (N64Recomp output) and runs on PC (Windows, Linux,
  macOS). The Switch work is the **host side**: CMake toolchain, platform layer,
  renderer, audio, input, file paths, memory, threads. The recompiled game code
  itself should need no change.
- Renderer: RT64 needs Vulkan (or D3D12/Metal). On Switch the only Vulkan is Mesa
  **NVK** built for Horizon: mesa-switch (danfromtico), patched by StevensND/nfsmw-nx,
  linked statically. antoxa2584x's Xbox ports use exactly this; their recipe is in
  `antoxa2584x/nfsu2-sw` `CLAUDE.md`, section "Vulkan build" (mesa commit 1a8c1a66d6f
  + `mesa/mesa-switch-nfsmw.patch`, glslang 15.4 for Switch, switch-sdl2's
  EGL/glapi/drm_nouveau filtered out of SDL2::SDL2).
- Horizon lessons from those ports: `docs/switch-port/horizon-notes.md` (copied from
  the xbox-recomp skill). The important ones here: thread priorities and cores (never
  core 3), never pass a buffer the app maps specially to Horizon IPC, SD logging through
  a RAM log device, title takeover (applet mode has ~400 MB), SDL audio thread priority.
- Eden (Switch emulator) cannot run NVK: test Vulkan builds on hardware only.

## Plan (each step ends with a short report in "Findings")

1. **Research.** Look for existing Switch ports of N64Recomp / N64ModernRuntime / RT64
   games (Zelda64Recomp, Banjo, others) and reuse what exists. List RT64's required
   Vulkan version, extensions and features, and compare with NVK on Switch. List every
   host dependency (`host/CMakeLists.txt`, `tools/RecompFrontend`, RmlUi, freetype,
   dbus, file dialogs, SDL2) and what devkitPro portlibs cover.
2. **Linux baseline** (x86, cloud): build with `./build.sh <rom>` when the ROM is
   available, run headless (lavapipe `VK_ICD_FILENAMES=.../lvp_icd.json`, or
   `SDL_VIDEODRIVER=offscreen`), confirm it boots.
3. **Switch build**: a `switch/build.sh` and a CMake path for `$DEVKITPRO/cmake/Switch.cmake`
   (devkitA64, libnx, switch-sdl2). Stub or replace what Horizon lacks (dbus, native
   file dialogs: use a fixed ROM path `sdmc:/switch/cbfd/conker.z64`; launcher). Get the
   recompiled C and runtime to link first, renderer after.
4. **NVK + RT64** on Switch, then audio, input (Joy-Con/Pro), saves under
   `sdmc:/switch/cbfd/`.
5. Hardware tests are done by the user; ask for `sdmc:/switch/cbfd/` logs.

## Cloud session limits

- No GPU and no ROM by default. The ROM (US `.z64`, SHA-1
  `4cbadd3c4e0729dec46af64ad018050eada4f47a`) comes from the user: ask where to get it.
- devkitPro is not installed: try the devkitPro pacman for Debian/Ubuntu or the
  `devkitpro/devkita64` container image; report if neither works.
- The full recompiled build is large: watch memory and use fewer jobs if the compiler is
  killed.

## Rules

- **Never commit the ROM or anything extracted from it.** This fork is public.
- Keep Switch changes behind `__SWITCH__` / a CMake option so the PC builds stay
  unchanged; check that the Linux build still works after each step.
- Commit in small steps on `switch`, push. Ask the user before opening a PR upstream.
- StevensND/nfsmw-nx is GPLv3: take ideas, not files (this repo's license: see `LICENSE`).

## Findings

### 2026-10-16: step 1, research

**Existing Switch ports of N64Recomp games** (found by web search; GitHub):
- **RaceWave46-Switch** (Wave Race 64), https://github.com/1weell/RaceWave46-Switch, branch
  `switch-port`, GPL-3.0 (ideas only). It is a libnx NRO with **RT64 on Vulkan through NVK** (danfromtico/mesa-switch),
  and it plays on hardware. It is the closest reference. It turns the RecompFrontend off and takes the ROM from an SD path. What it does:
  - A host `dxc` and a host `file_to_c`, because RT64's shaders are made during the build.
  - Flags and defines: `-mtp=soft -ftls-model=local-exec`, `VK_USE_PLATFORM_VI_NN`,
    `FMT_USE_FALLBACK_FILE=1` / `FMT_OS=OFF`, and no `-msse4.1`.
  - Link line: `rt64 SDL2 $sdk/libEGL.a $sdk/libglapi.a nx -Wl,-u,vk_icdGetInstanceProcAddr vulkan expat zstd z nx m stdc++`.
    It does not use whole-archive on `libvulkan.a`, and it does not link devkitPro's old EGL/glapi/drm_nouveau.
  - Changes in plume:
    - volk's pointers are renamed so they don't clash with the static driver's `vk*` functions.
    - `vkCreateViSurfaceNN(nwindowGetDefault())`.
    - The size comes from `nwindowGetDimensions`, because SDL reports 0.
    - Swapchain usage leaves out SAMPLED, and preTransform falls back to `currentTransform`.
- **DK64Recomp-NX**, https://github.com/Trinalord777/DK64Recomp-NX (branch `nx`). It is a libnx NRO that plays on hardware,
  but it uses GLideN64 on OpenGL, not RT64. Its hardware lessons:
  - libnx has no `pthread_detach`.
  - `std::filesystem::copy_file` fails on the SD card (`fchmod`).
  - SDL audio needs 1024-frame buffers.
  - nvdrv transfer memory had to go from 8 to 32 MB.
  - Title takeover is required.
- No Switch port was found for Zelda64Recomp, Banjo, Starfox64, MK64, Dr. Mario, Goemon or Chameleon Twist.
  Upstream already ships Linux ARM64 builds. No N64ModernRuntime fork has Horizon support.
- Other NVK ports: https://github.com/danfromtico/mesa-switch (Mesa 26.2.x, static loaderless `libvulkan.a`,
  meson `-Dplatforms=switch -Dvulkan-drivers=nouveau`, needs Rust), and https://github.com/PalindromicBreadLoaf/nxvk.
  StevensND/nfsmw-nx `mesa/mesa-switch-nfsmw.patch` applies on mesa-switch 1a8c1a66d6f and fixes NAK bugs.
  The exact recipe is in antoxa2584x/nfsu2-sw `CLAUDE.md`, "Vulkan build".

**RT64 (plume) Vulkan needs vs NVK on the Tegra X1** (from reading the code: `tools/rt64/src/contrib/plume/plume_vulkan.cpp`;
NVK's list is from mesa-switch's `nvk_physical_device.c`, not from a `vulkaninfo` on hardware):

| RT64 / plume needs | NVK on Switch (GM20B) |
|---|---|
| Instance at Vulkan 1.2; core 1.1 `*2` queries; VMA at 1.2 | Vulkan 1.4 advertised |
| Only `VK_KHR_swapchain` required; descriptor_indexing, scalar_block_layout, robustness2, BDA, sample_locations, load_store_op_none, present_id/wait and RT are optional | All present except present_id/present_wait and RT |
| In practice: descriptor indexing with update-after-bind (8192-texture array, `NonUniformResourceIndex`), dualSrcBlend (N64 blending), depthClamp, samplerAnisotropy, MSAA sampled images | Descriptor indexing, dualSrcBlend, depthClamp: yes. Update-after-bind limits: check on hardware |
| Not used: timeline semaphores, dynamic rendering, sync2, geometry shaders, Int64/Float64, Float16 | (NVK has no shaderFloat16 on Maxwell: not needed) |
| Shaders: SPIR-V made by DXC at build time (`vulkan1.0`); specialisation at run time by re-spirv (pure C++). **No DXC or shader compiler on the console.** Ubershaders until a specialised pipeline is ready | Pipeline creation costs ~59 ms without a cache (nfsmw): keep a VkPipelineCache |
| Presentation | NVK's Switch WSI: FIFO and IMMEDIATE only, 3 images, one queue, syscalls per submit/fence |

**Host dependencies and what devkitPro has** (the devkitpro/devkita64 Docker image, 2026-10: GCC 15.2, newlib 4.6,
libnx 4.12):

| Dependency | Used by | Switch |
|---|---|---|
| SDL2 (window, input, audio) | host, RT64, recompui, recompinput | switch-sdl2 2.28.5. No SDL_Vulkan (the surface is made in plume) and no SDL_GetPrefPath |
| freetype | RmlUi (recompui) | switch-freetype 2.13.3 |
| zstd | RT64 (bundled, built from source) | built from source |
| RmlUi 6.0, lunasvg/plutovg | recompui (bundled) | built from source. plutovg's font-file mmap is turned off |
| nativefiledialog-extended (GTK/dbus portal) | RT64, launcher (Load ROM, mods) | none: `host/switch/nfd_stub.c`, and the ROM comes from `sdmc:/switch/cbfd/conker.z64` |
| dbus, X11 | nfd portal, RT64's X11 path | not needed (SDL path, nfd stub) |
| Vulkan loader (volk dlopen) | plume | none: `volkInitializeCustom(vk_icdGetInstanceProcAddr)` from the NVK linked in |
| mmap/mprotect (4 GB RDRAM reservation, TLB pages, mods' code patching, sljit, RT64 mapped files) | librecomp, host overlays, RT64 | none: libnx virtmem + code memory (`nx_memory.cpp`), read into memory (RT64), mods off |
| dlopen (native mods) | librecomp | none: native mods are off |
| execinfo/backtrace, dl_iterate_phdr | save states, crash handler | `_Unwind_Backtrace` + `__start__`/`__end__`; libnx exception handler |
| SSE (RSP vector unit) | audio microcode | sse2neon (already used on Apple Silicon) |
| pthreads, std::thread, std::filesystem | everywhere | yes, through libnx. **`pthread_detach` returns ENOSYS** (libnx has no `__syscall_thread_detach`), so `std::thread::detach()` throws: the runtime's timer detaches. `posix_memalign` and `timegm` are missing too. All three are in `host/switch/switch_compat.c` |

### 2026-10-16: port plan

The PC builds are unchanged. Every Switch change is behind `__SWITCH__`, `NINTENDO_SWITCH` or `CMAKE_CROSSCOMPILING`.
Switch changes to the submodules are in `switch/patches/*.patch`, which `switch/build.sh` applies on top of `recomp/*.patch`.

- **This repo:**
  - `host/CMakeLists.txt`:
    - `CONKER_HOST_ONLY` compiles everything without the recompiled game.
    - The Switch block adds `_GNU_SOURCE` and forces `switch_compat.h` into every file.
    - nfd stub, volk aliases generated from `volk.h`, the sljit config, and the NRO with NVK.
  - `host/switch/`:
    - `switch_nx.cpp`: SD log, flushed twice a second, the exception handler, and an applet-mode warning.
    - `switch_compat.c`, `nfd_stub.c`, `nx_sljit*`, `include/dlfcn.h`.
  - `host/src/main.cpp`: data folder `sdmc:/switch/cbfd`, fixed ROM path, no unbuffered stdout.
  - `host/src/overlays.cpp`: TLB pages through `recomp::nx::commit`.
  - `host/src/frontend.cpp`: full-screen SDL window without `SDL_WINDOW_VULKAN`.
  - `switch/build.sh`.
- **N64ModernRuntime** (`n64modernruntime-switch.patch`):
  - RDRAM: a 4 GB virtmem reservation; RDRAM and the TLB pages are backed by code memory mapped at their place
    (`svcCreateCodeMemory` + `MapOwner`, as the Xbox ports do). `mem_size` is 64 MB, not 512 MB.
  - Semaphores use the POSIX branch, and `WindowHandle = SDL_Window*`.
  - Thread priorities are 0x29-0x2E, spread over cores 0-2.
  - Save-state stack hashing uses `_Unwind_Backtrace`.
  - Mods: no dynamic libraries, and function patching is off.
  - Saves and config (`files.cpp`) rotate their backups with `rename`, not `copy_file`, which fails on the SD card
    (DK64Recomp-NX). There are other `copy_file` calls still to check on hardware: `host/src/rom_versions.cpp`, `main.cpp`'s
    migration, and recompui's mod installer.
- **RT64** (`rt64-switch.patch`):
  - Host DXC (`CMAKE_HOST_SYSTEM_PROCESSOR`), and a host `file_to_c` (`RT64_HOST_FILE_TO_C`).
  - nfd only when no target exists, and no texture tools when cross-compiling.
  - Thread priorities and cores.
  - Mapped files are read into memory.
  - `sdmc:/switch/<id>` for its user path, and no SDL_WINDOW_VULKAN.
- **plume** (`plume-switch.patch`):
  - `VK_USE_PLATFORM_VI_NN`, a `vkCreateViSurfaceNN(nwindowGetDefault())` surface, and the size from `nwindowGetDimensions`.
  - `volkInitializeCustom(vk_icdGetInstanceProcAddr)`.
  - The swapchain only asks for the usage and transform the surface supports.
- **RecompFrontend** (`recompfrontend-switch.patch`):
  - SDL include paths for NintendoSwitch, and the app folder is the working directory.
  - No "open mods folder", and no dynamic-library mods.
- **Stubbed or off for now:**
  - Mods: no code patching or LiveRecomp JIT; `.nrm` data and texture packs still load.
  - File dialogs.
  - The crash backtrace (the exception handler prints the registers and the PC relative to the ELF).
- **Next:**
  1. Link with the recompiled code: needs the ROM.
  2. Build mesa-switch NVK (Docker, Rust) and link the NRO; test a clear screen first.
  3. Hardware: memory (`nx_memory.cpp`; is `MapOwner` at an arbitrary address allowed?), the Vulkan surface,
     audio (SDL, 1024 frames), and Joy-Con/Pro input.
  4. A pipeline cache (`VkPipelineCache`) on the SD card.
  5. Mods through a writable alias of `.text` and libnx Jit, if wanted.

### 2026-10-16: step 2, Linux baseline (x86-64, cloud, no GPU)

- ROM: the user's private repo `rashevskyv/conker-data` (`n64/conker.z64`, SHA-1 matches). Clone it outside this repo,
  for example to `/home/user/conker-data`. `./build.sh /home/user/conker-data/n64/conker.z64` takes about 12 minutes on 4 cores, with no errors.
- Headless run: `host/build/ConkerRecomp --headless --rom <rom> --seconds 60` boots the game.
  - In 60 s: 3595 VIs (59.9 Hz), 1794 display lists.
  - Scenes 0x25, 0x21 and 0x1D: the intro.
- With RT64 on lavapipe (`apt install mesa-vulkan-drivers xvfb`):
  `VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json xvfb-run -a -s "-screen 0 1600x900x24" ./ConkerRecomp --rom <rom> --seconds 90`
  - In 90 s: 5342 VIs.
  - It draws correctly. A screenshot at 65 s (Xvfb plus `import -window root`) shows the widescreen bar scene. The screenshots are not committed, because they come from the ROM.
- **Crash on quit, already in upstream:** the headless run gets SIGSEGV in `timer_thread` (ultramodern `timer.cpp`).
  - The timer thread is detached, and it still reads an `OSTimer` in RDRAM after `recomp::start` has unmapped it.
  - It only happens on quit. On the Switch, `recomp::nx::release()` unmaps in the same way, so expect an exception on quit.
  - Fix: join the timer, or don't free RDRAM on quit.
- On quit, the window build also aborts in D-Bus (`dbus_connection_unref`, from NFD_Quit's portal, with no session bus). This only
  happens in this environment.

### 2026-10-16: step 3, the Switch build without the ROM

- devkitPro: its pacman (apt.devkitpro.org, pkg.devkitpro.org) is blocked by the proxy, with a 403. The
  `devkitpro/devkita64` Docker image works: `docker create`, then `docker cp` `/opt/devkitpro` out of it. `dockerd` must be started first.
- `switch/build.sh --host-only` (`CONKER_HOST_ONLY=ON`, RT64 on): **everything compiles for the Switch**.
  That covers ultramodern, librecomp, N64Recomp/LiveRecomp, RT64 (with its shaders made by the host DXC and file_to_c),
  plume, zstd, re-spirv, RmlUi, lunasvg, recompui, recompinput, and every host source, the launcher included.
  Nothing is linked yet.
- The Linux build with every Switch patch applied (`CONKER_HOST_ONLY=ON`, clang) still compiles.
- Not done, and why:
  - **Linking the NRO**: it needs `RecompiledFuncs/` (from the ROM) and a mesa-switch NVK build (`NVK_SDK`).
    The CMake for it is written but untested.
  - **`CONKER_RT64=OFF` (headless)**: it is already broken on PC. `widescreen.cpp`, `render_fixes.cpp` and `mouse_camera.cpp`
    use SDL and recompinput without `CONKER_RT64`, so it is not a Switch target.
- CMake pitfall: it removes a repeated `-include` flag, which leaves the second header as a lone input file.
  That gives "cannot specify -o with multiple files". Use `"SHELL:-include <file>"`.
