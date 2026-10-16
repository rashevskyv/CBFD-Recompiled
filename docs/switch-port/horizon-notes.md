# Switch (Horizon): what only the console shows

Source: `nfsu2-sw/CLAUDE.md` ("Switch (Horizon) findings", "Vulkan build", "Performance findings").
The fixes below are in the toolkit (`xboxrecomp/src/platform/win32_compat.c`,
`src/kernel/*`) and the donor's `src/switch_nx.c`. Copy `switch_nx.c` per game; rename only
the `<x>` paths and file names.

## Test order

1. Linux plays the milestone, then Eden with the **GL** NRO (Eden cannot run NVK: no GPU
   submission completes), then hardware with the Vulkan NRO.
2. Launch with **title takeover** (hold R on a game). Applet mode has ~400 MB: too little.
3. SD layout: `sdmc:/switch/<x>/{<x>-vulkan.nro, game/ (unpacked), save/, <x>_env.txt, <x>_log.txt}`.
4. Hardware logs come back as `<x>_log.txt`; the user drops them in `switch_sd/switch/logs/`.
   `nxlink -s` sends the log to the host instead.

## Platform facts

| Fact | Consequence / fix |
|---|---|
| No mmap; `svcCreateSharedMemory` → 0x4201 (Eden) / may kill on hardware | guest RAM is **code memory** (`svcCreateCodeMemory` + `MapOwner`); `RECOMP_NX_SHM=1` opts into shared memory |
| Code memory cannot alias | second views via `svcMapProcessMemory`; else 0x80000000 window folded onto low RAM; tiled aperture 0xF0000000+ folded (`XBOX_DEV_FOLD`) for sections in `--mmio-sections` |
| AArch64 atomics fault on unaligned addresses (exception 259); Eden does not model it | `RECOMP_ATOMIC_*` spinlock fallback (toolkit) |
| Horizon IPC with guest (code) memory jams the fs service → whole console freezes | `host_read`/`host_write` through a .bss bounce buffer (`kernel_file.c`); **never** hand guest memory to IPC |
| Leaked directory handles → ams.mitm `std::abort` | `xbox_dir_forget()` on NtClose |
| libnx threads start at priority 59 on preferred core 0 | `xbox_nx_spread_thread()` deals cores 0-2; **never core 3** (does not time-slice 59) |
| SDL audio TIME_CRITICAL maps to 59 → crackle | linker `--wrap` raises it to 0x2B (`RECOMP_NX_AUDIO_PRIO=0` off) |
| Main thread stack too small for lifted code | game runs on a 16 MB pthread |
| Flushing stderr to SD each line: 33 s boot | `log:` devoptab in RAM, flushed every 0.5 s; `NFSU2_LOG_SYNC=1` for hangs |
| newlib shares the dtoa buffer between `%f` calls | integer-formatted timestamps |
| `SDL_CreateWindow` hangs after a libnx framebuffer was open | loading screen drawn on the SDL window the renderer adopts |
| Docked EGL surface 1920x1080 while SDL says 1280x720 | query the real surface size |
| sys-clk / Horizon OC reset clocks every tick → freeze (issue #25) | `NFSU2_*_MHZ` ignored when they run; `NFSU2_CLOCK_FORCE=1` overrides |
| NVK linked statically → NX-FPS has nothing to hook | `switch_saltynx.c` writes NX-FPS's shared block |

## Toolchain (WSL)

- `DEVKITPRO=/opt/devkitpro`: `switch-dev`, `switch-sdl2`, `switch-mesa`.
- Vulkan: mesa-switch NVK (static `libvulkan.a`, commit 1a8c1a66d6f + StevensND/nfsmw-nx
  `mesa/mesa-switch-nfsmw.patch`) installed as `NVK_SDK`; glslang 15.4 for Switch as
  `GLSLANG_DIR`. switch-sdl2's EGL/glapi/drm_nouveau are filtered out of `SDL2::SDL2`.
  Recipe: `nfsu2-sw/CLAUDE.md`, "Vulkan build".
- Minimal NVK check: a clear-screen NRO (`vktest`) before the game.

## Debugging on hardware

- Crash: `__libnx_exception_handler` prints guest state. Exception 257 = bad address
  (often a stale 0xAAAAAAAA pointer through the 0x80xxxxxx window, or the tiled aperture),
  259 = unaligned access.
- Hang: `RECOMP_WATCHDOG_SECS=50` + `RECOMP_WATCHDOG_KEEP=1` in `<x>_env.txt`.
- Profile: `RECOMP_NX_PROFILE=1` (busy threads) or `2` (all threads > 2%) → `prof.bin`;
  `python3 tools/prof_report.py prof.bin build-switch/<name>.elf <x>_log.txt`.
- `[perf]` lines every 10 s: fps, APU frames/s (1500 = real time), audio gaps, GIL waits.
- Save files: copy the console's `UDATA/<titleid>/` into `/root/<x>/game/UDATA/` to reproduce
  on Linux.

## Performance levers that worked (NFSU2 order)

1. Vblank at a true 59.94 Hz (was ~46 Hz).
2. Kernel time queries and IRQL changes keep the GIL (`RECOMP_KERNEL_FAST`).
3. `RECOMP_GIL_EAGER=1`: main and time-critical guest threads pre-empt.
4. Frame lag (`RECOMP_FRAME_LAG=1`) when the title waits on the frame it just built.
5. Registers / x87 top in C locals (translator, default on), native hot leaves.
6. Vulkan renderer: program/pipeline caches (`progcache.bin`, `vkpipes.bin`), DXT uploaded
   compressed, instancing of repeated draws.
7. LTO (`LTO=1`), clocks via `NFSU2_CPU_MHZ`/`GPU`/`MEM` in `<x>_env.txt`.

Eden fps is not a measure of game code; measure on hardware or on a repeatable Linux scene.
