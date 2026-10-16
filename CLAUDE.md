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

(none yet)
