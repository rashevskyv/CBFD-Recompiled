# Building the Switch NRO on your own machine (Linux or WSL)

The NRO holds the recompiled game, made from your ROM, so it is built where the ROM is: on your
computer. This is the whole sequence on Ubuntu 24.04 (native or WSL 2), tested in the cloud
session that wrote it (except the WSL-specific notes).

## Where to put it (WSL)

Clone into the WSL home directory (`~/`), not under `/mnt/c` or `/mnt/d`: Windows drives are slow
from WSL, and the build writes a few GB. `switch/build.sh` warns when it runs under `/mnt/`.

| What | Size |
|---|---|
| This repository with its submodules | ~1.5 GB |
| devkitPro (switch-dev and the portlibs below) | ~0.6-1.2 GB |
| `host/build-switch` (the Switch build) | ~2.5 GB |
| mesa-switch, if you build NVK yourself (source and two build dirs) | ~2 GB; the result is 94 MB |

Memory: 8 GB is enough at the default `-j$(nproc)`. The largest recompiled file needs ~200 MB to
compile, RT64's C++ files more, the final link ~1 GB.

## 1. Tools (once)

```sh
sudo apt install git cmake ninja-build g++ python3 curl
# devkitPro: https://devkitpro.org/wiki/devkitPro_pacman, then
sudo dkp-pacman -S switch-dev switch-sdl2 switch-freetype switch-libexpat switch-zlib \
    switch-libzstd switch-bzip2 switch-libpng switch-pkg-config switch-tools
```

## 2. The code

```sh
cd ~
git clone --recursive -b switch https://github.com/rashevskyv/CBFD-Recompiled.git
cd CBFD-Recompiled
```

## 3. NVK (Mesa's Vulkan driver for the Switch)

Either build it (about 20 minutes, needs Rust and LLVM 15; the list is at the top of the script):

```sh
NVK_SDK=$HOME/nvk-sdk switch/build-nvk.sh ~/mesa-switch
```

or unpack a prebuilt `libvulkan.a` (mesa-switch `0ff5207`, built by `switch/build-nvk.sh`) so that
it is at `$HOME/nvk-sdk/lib/libvulkan.a`. Either way, `NVK_SDK` is the folder holding `lib/`.

## 4. Build

```sh
export NVK_SDK=$HOME/nvk-sdk
switch/build.sh --vktest                      # vktest.nro: the NVK check, first
switch/build.sh --rom /path/to/conker.z64     # recompiles from the ROM (once), then the NRO
```

`switch/build.sh` checks everything above first and lists what is missing. After the first run,
`switch/build.sh` alone rebuilds (the ROM is only needed again when `recomp/` changes). To build
somewhere else: `SWITCH_BUILD_DIR=~/cbfd-build switch/build.sh`.

## 5. Onto the console

With ftpd (or sys-ftpd) running on the Switch, on the same network:

```sh
switch/deploy.sh 192.168.1.50            # NROs and assets/ into sdmc:/switch/cbfd/
switch/deploy.sh 192.168.1.50 logs       # cbfd_log.txt, vktest_log.txt into switch/logs/
```

Put your ROM on the SD card once, as `sdmc:/switch/cbfd/conker.z64`. Start the NROs with title
takeover (hold R while starting a game): the album applet has too little memory. What to test,
and what to look for in the logs: `docs/switch-port/hardware-tests.md`.

nxlink also works, but under WSL 2 only with networking mode "mirrored" (`.wslconfig`), since the
console has to connect back to WSL for the output.
