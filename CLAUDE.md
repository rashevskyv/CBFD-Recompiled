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
  1. Done: the recompiled code links. Only NVK and its EGL are missing.
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
- With the ROM, `NVK_SDK=<empty archives> switch/build.sh` compiles all of `RecompiledFuncs/` for aarch64 (864 steps).
  **The link fails only on what mesa-switch provides**: `vk_icdGetInstanceProcAddr` (NVK), and the `egl*`
  functions that switch-sdl2 calls, which come from mesa-switch's `libEGL.a`. Everything else links: the game, the runtime,
  RT64 and the launcher.
  - Pitfall: every directory's `find_package(SDL2)` makes its own imported `SDL2::SDL2-static`. That target has to be
    filtered once and made `IMPORTED_GLOBAL`, or rt64 still links devkitPro's old `-lEGL -lglapi -ldrm_nouveau`.
    `SDL2::SDL2` is an alias of it.
- Not done, and why:
  - **The NRO itself**: it needs a mesa-switch NVK build (`NVK_SDK`: `lib/libvulkan.a`, `libEGL.a`, `libglapi.a`).
  - **`CONKER_RT64=OFF` (headless)**: it is already broken on PC. `widescreen.cpp`, `render_fixes.cpp` and `mouse_camera.cpp`
    use SDL and recompinput without `CONKER_RT64`, so it is not a Switch target.
- CMake pitfall: it removes a repeated `-include` flag, which leaves the second header as a lone input file.
  That gives "cannot specify -o with multiple files". Use `"SHELL:-include <file>"`.

### 2026-10-16: step 4, NVK built, the NRO links (not yet run on hardware)

- **Renderer choice:** NVK first, then a deko3d backend for plume. The user expects deko3d to be much faster (it is
  for the Switch PS2 emulator). The design and the work for that are in `docs/switch-port/deko3d-design.md`:
  - the shaders go through DXC → spirv-cross → uam;
  - the 8192-texture array stays, through bindless handles;
  - estimate 2-3 months.
- **NVK:** danfromtico/mesa-switch `0ff5207` (Mesa 26.2.4, newer than the 1a8c1a66d6f the Xbox ports use).
  - We do *not* apply nfsmw-nx's patch, because it is GPLv3.
  - `switch/build-nvk.sh` builds it on Ubuntu 24.04 without mesa-switch's Docker image. That image's `build-switch.sh` is
    out of date. Pitfalls:
    - Rust for the console must be `aarch64-unknown-linux-gnu`: `MESA_SWITCH_RUST_TARGET` is read by
      `rustc-switch-wrapper.sh`, and only `build-unified.sh` sets it. Without it, NAK's Rust objects are x86-64, and the
      link gives "file in wrong format".
    - Proc-macros run on the build machine, so a native file must point `rust` at the plain rustc. Otherwise
      "rust-lld: --fix-cortex-a53-843419 is only supported on AArch64".
    - meson picks Ubuntu's llvm-config-18. The host tools need `llvm-config` 15 through a native file.
    - `rustfmt` is needed (not fatal).
  - The result is `libvulkan.a`, 94 MB, 921 aarch64 objects (merged from libnvk, NAK, NIR, …).
- **`vktest.nro`** (`switch/build.sh --vktest`, `switch/vktest/vktest.c`) links: instance → device → VI swapchain →
  coloured clears for 10 s. It logs to `sdmc:/switch/cbfd/vktest_log.txt`.
- **`ConkerRecomp.nro`** links (35 MB NRO, 330 MB ELF):
  - The link is `-Wl,-u,vk_icdGetInstanceProcAddr libvulkan.a expat zstd z nx m stdc++`.
  - switch-sdl2's EGL calls (only for GL windows) go to `host/switch/sdl_egl_stubs.c`.
  - mesa-switch defines `posix_memalign`, so ours is weak.
- **Thread stacks:** libnx's `__syscall_thread_create` gives a thread made without a stack size **128 KB**. That covers
  every `std::thread`: the game's, RT64's and the launcher's. Windows gives 1 MB. `switch_nx.cpp` raises it to 2 MB through
  `-Wl,--wrap=__syscall_thread_create`. Found by disassembling libnx's `newlib.o` (`mov x0, #0x20000`).
- The NRO contains the recompiled game: it is built from the ROM, so it is never committed or published.

### 2026-10-16: fixes that need no hardware; local builds

- **Pipeline cache:** plume had none, so every pipeline was compiled from scratch on every run (~60 ms each on
  NVK, per nfsmw).
  - `plume-switch.patch` adds a `VkPipelineCache`, used only when `PLUME_VULKAN_PIPELINE_CACHE_FILE` is defined.
  - The Switch defines it as `sdmc:/switch/cbfd/vkpipecache.bin`.
  - The cache is loaded after `vkCreateDevice`, and saved at most every 10 s while pipelines are being made (the HOME menu
    may skip the exit) and again on release.
  - It is written to `.new` and then renamed, so a crash mid-write keeps the old file.
  - The PC is unchanged: no cache.
- **Crash on quit (timer thread):** on the Switch, RDRAM stays mapped at quit, because the process ends right after.
  On PC it is unchanged (the upstream crash).
- **`copy_file` on the SD card:**
  - `conker::copy_file` (main.cpp) copies the bytes on the Switch. It is used by `rom_versions.cpp` and the data migration.
  - recompui's mod installer does the same.
  - Saves (`files.cpp`) already rotate with `rename`.
- **Audio:** SDL chunks are 1024 frames on the Switch, not 256 (DK64Recomp-NX). The log says `[audio] N Hz, …-frame chunks`.
- **Log lines for the hardware tests:**
  - `[nx] memory: N MB` (total, and used at start).
  - `[nx_memory] reserved/mapped …` for every success, not only failures.
- **`switch/build.sh`:**
  - It checks everything first and lists what is missing: devkitPro and the portlibs, NVK_SDK, that RT64's DXC runs,
    RecompiledFuncs.
  - `--rom` recompiles from the ROM without building the PC version.
  - `SWITCH_BUILD_DIR` moves the build, and the script warns under `/mnt/`.
  - Measured:
    - the largest recompiled file needs 190 MB and 13 s to compile for aarch64;
    - the final link needs ~1 GB and 75 s;
    - `host/build-switch` is 2.2 GB.
- Docs for the user's own machine:
  - `docs/switch-port/local-build.md` (WSL: clone, devkitPro, NVK, `switch/build.sh --rom`, `deploy.sh`);
  - `docs/switch-port/hardware-tests.md` (vktest, then the game: which log lines to look for).

### 2026-10-16: first hardware run (local WSL build, user's console, handheld, title takeover)

- **Local build** (WSL Ubuntu 24.04, 16 cores), with `docs/switch-port/local-build.md`:
  - There was no prebuilt NVK, so `switch/build-nvk.sh` built it here (`libvulkan.a`, 89 MB, mesa-switch `0ff5207`).
    Its prerequisites came from the script header. SPIRV-Tools `vulkan-sdk-1.3.290.0` was built from source (2024.3.1).
  - Pitfall: rustup now needs `--component rust-src,rustfmt` (with a comma). The header said `rust-src rustfmt`,
    and rustup reads `rustfmt` as a toolchain name. The header is fixed.
  - Pitfall: WSL's disk is one `ext4.vhdx` file on a Windows drive. Here it was on D:, which was full. The clone filled
    it, and WSL gave `Input/output error` on every write until space was freed. Check the free space on the vhdx's drive
    first, not only `df /` in WSL.
  - `switch/build.sh --vktest`, then `switch/build.sh` (RecompiledFuncs from `recomp/recompile.py`): both link.
    `deploy.sh <ip> push` works with the console's FTP server on port 5000.
- **vktest.nro: NVK works.**
  - Every call returns 0. `GPU: NVIDIA Tegra X1 (GM20B) (NVK GM20B), Vulkan 1.4.354, driver 0x6802004`.
  - `features: dualSrcBlend 1, depthClamp 1, independentBlend 1, samplerAnisotropy 1, sampleRateShading 1`.
  - `surface: 1280x720, images 3-3, usage 0x8009F, transforms 0x1`, `surface format 37`.
  - `done: 603 frames` in 10 s (60 fps, FIFO), then `exit`.
  - **After it quit, the system showed an error, and the menu (kefir-hub) crashed.** vktest returned without
    destroying anything (swapchain, surface, device, instance). It now destroys them in reverse order and logs
    `destroyed`. Not yet tested on hardware.
- **ConkerRecomp.nro: it boots and plays.** The launcher shows, input works, and the game reaches the intro
  (scenes 0x25, 0x21, 0x1D, 0x18).
  - `[nx] memory: 3285 MB` (title takeover).
  - `[nx_memory] reserved 0x100000000 bytes`, then `mapped 0x4000000` (RDRAM), `0x210000` and `0x10000` (TLB pages).
    **Code memory with MapOwner at a chosen address works** (the open question in "port plan").
  - RT64 runs on NVK: `Pipeline cache: 0 bytes read` on the first run. `vkpipecache.bin` was 2.9 MB after it.
  - `[audio] 48000 Hz, 1024-frame chunks`, then `22020 Hz` in game.
  - Saves and config: the `.json` files and their `.bak` copies are written to `sdmc:/switch/cbfd/`.
  - **Speed: about 24 fps, sometimes up to 30** (Show FPS). The user wants 60, or a stable 30. The settings were the
    defaults: `msaa_option MSAA2X`, `res_option Auto`, `conker_detail AlwaysHighest`, `rr_option Display`. Whether the
    CPU or the GPU is the limit is not measured yet.
  - **The pause menu shows wrong graphics.** The user took a screenshot. It is not read yet.
  - `[frontend] SDL_CreateWindow failed: Could not get EGL display`, then `SDL_GetWindowDisplayIndex failed. Error:
    Invalid window.` (twice). The SDL window is never made, because the EGL stubs fail. RT64 still presents
    through `nwindowGetDefault()`. Anything that asks SDL for the window or its size gets nothing.
  - SDL lists 8 `Switch Controller`s: switch-sdl2 opens all 8 npad slots.
