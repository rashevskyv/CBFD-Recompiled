#!/bin/sh
# Builds Conker's Bad Fur Day: Recompiled for the Nintendo Switch (a libnx NRO), on Linux or WSL:
#   switch/build.sh [--rom path/to/rom.z64] [--host-only | --vktest]
#   --rom PATH   recompile the game from your ROM first (when RecompiledFuncs/ is missing or out of
#                date), without building the PC version; the ROM is copied to conker/baserom.us.z64
#   --host-only  compile everything but the recompiled game, and link nothing (no ROM, no NVK)
#   --vktest     only build vktest.nro, the NVK clear-screen check (switch/vktest/)
# Needs: devkitPro ($DEVKITPRO, default /opt/devkitpro) with switch-dev and the portlibs listed by
# the check below, Mesa's NVK for the Switch in $NVK_SDK (switch/build-nvk.sh, or a prebuilt one),
# and git, cmake, ninja, a C++ compiler and python3 for this machine.
# The build goes to $SWITCH_BUILD_DIR (default host/build-switch, about 2.5 GB); keep it, and this
# checkout, off /mnt/<drive> under WSL (Windows drives are slow there). See docs/switch-port/local-build.md.
set -e

cd "$(dirname "$0")/.."
ROOT=$(pwd)
HOST_ONLY=OFF
VKTEST=
ROM=
while [ $# -gt 0 ]; do
    case "$1" in
        --host-only) HOST_ONLY=ON ;;
        --vktest) VKTEST=1 ;;
        --rom) shift; ROM=$1 ;;
        -h|--help) sed -n '2,14p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
    shift
done
JOBS=$(nproc 2>/dev/null || echo 4)
: "${DEVKITPRO:=/opt/devkitpro}"
export DEVKITPRO
BUILD=${SWITCH_BUILD_DIR:-host/build-switch}
[ "$HOST_ONLY" = ON ] && BUILD=${SWITCH_BUILD_DIR:-host/build-switch-host-only}

step() { printf '\n==> %s\n' "$*"; }
fail() { printf '\nError: %s\n' "$*" >&2; exit 1; }

# ---------------------------------------------------------------- prerequisites
# Everything missing is listed at once, with where it comes from.
step "Checking the prerequisites"
missing=
need() { missing="$missing\n  - $*"; }
for cmd in git cmake ninja c++ python3; do
    command -v "$cmd" >/dev/null 2>&1 || need "$cmd (on Ubuntu: apt install git cmake ninja-build g++ python3)"
done
if [ ! -f "$DEVKITPRO/cmake/Switch.cmake" ] || [ ! -x "$DEVKITPRO/devkitA64/bin/aarch64-none-elf-gcc" ]; then
    need "devkitPro with devkitA64 and libnx in $DEVKITPRO (https://devkitpro.org/wiki/devkitPro_pacman, then: dkp-pacman -S switch-dev)"
else
    for lib in libnx/lib/libnx.a portlibs/switch/lib/libSDL2.a portlibs/switch/lib/libfreetype.a \
               portlibs/switch/lib/libexpat.a portlibs/switch/lib/libz.a portlibs/switch/lib/libzstd.a \
               portlibs/switch/lib/libbz2.a portlibs/switch/lib/libpng.a portlibs/switch/bin/aarch64-none-elf-pkg-config \
               tools/bin/elf2nro tools/bin/nacptool; do
        [ -e "$DEVKITPRO/$lib" ] || need "$DEVKITPRO/$lib (dkp-pacman -S switch-dev switch-sdl2 switch-freetype switch-libexpat switch-zlib switch-libzstd switch-bzip2 switch-libpng switch-pkg-config switch-tools)"
    done
fi
if [ "$HOST_ONLY" = OFF ] && [ ! -f "${NVK_SDK:-/nonexistent}/lib/libvulkan.a" ]; then
    need "Mesa's NVK: NVK_SDK=<dir with lib/libvulkan.a> (switch/build-nvk.sh builds it)"
fi
if [ -z "$VKTEST" ]; then
    # RT64 compiles its shaders with the DXC it ships, for this machine.
    case "$(uname -m)" in
        x86_64) DXC_ARCH=x64 ;;
        *) DXC_ARCH=arm64 ;;
    esac
    DXC_DIR=tools/rt64/src/contrib/dxc
    if [ -x "$DXC_DIR/bin/$DXC_ARCH/dxc-linux" ] && \
       ! LD_LIBRARY_PATH="$DXC_DIR/lib/$DXC_ARCH" "$DXC_DIR/bin/$DXC_ARCH/dxc-linux" --version >/dev/null 2>&1; then
        need "RT64's DXC doesn't run here ($DXC_DIR/bin/$DXC_ARCH/dxc-linux)"
    fi
    if [ "$HOST_ONLY" = OFF ] && [ -z "$ROM" ] && [ ! -f RecompiledFuncs/inputs.sha256 ]; then
        need "the recompiled game (RecompiledFuncs/): run with --rom path/to/your/conker.z64"
    fi
fi
[ -z "$ROM" ] || [ -f "$ROM" ] || need "a ROM at $ROM"
if [ -n "$missing" ]; then
    printf "Missing:$missing\n" >&2
    exit 1
fi
case "$ROOT/$BUILD" in
    /mnt/*) echo "  Note: building under /mnt/ (a Windows drive) is slow, and needs ~2.5 GB there. A checkout in the WSL home directory is better." ;;
esac
echo "  all there"

# ---------------------------------------------------------------- patches
# The PC patches (recomp/*.patch, as build.sh applies them), then the Switch ones on top
# (switch/patches/). Each tool is reset when it holds anything else.
patch_tool() {
    tool=$1; shift
    last=
    for patch in "$@"; do last=$patch; done
    # Done already if the last (Switch) patch is applied.
    if git -C "$tool" apply --reverse --check "$ROOT/$last" 2>/dev/null; then
        return
    fi
    git -C "$tool" reset --hard -q
    git -C "$tool" clean -fdq
    for patch in "$@"; do
        git -C "$tool" apply "$ROOT/$patch" || fail "couldn't apply $patch to $tool."
    done
    echo "  patched $tool"
}

if [ -n "$VKTEST" ]; then
    git submodule update --init tools/rt64
    git -C tools/rt64 submodule update --init src/contrib/plume
    git -C tools/rt64/src/contrib/plume submodule update --init contrib/Vulkan-Headers
    cmake -S switch/vktest -B host/build-switch-vktest -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/Switch.cmake" -DCMAKE_BUILD_TYPE=Release \
        -DCONKER_NVK_SDK="$NVK_SDK"
    cmake --build host/build-switch-vktest
    printf '\nDone: %s/host/build-switch-vktest/vktest.nro\n' "$ROOT"
    exit 0
fi

step "Getting the submodules"
git submodule update --init --recursive
step "Patching the tools"
patch_tool tools/N64Recomp recomp/n64recomp.patch
patch_tool tools/N64ModernRuntime recomp/n64modernruntime.patch switch/patches/n64modernruntime-switch.patch
patch_tool tools/rt64 recomp/rt64.patch switch/patches/rt64-switch.patch
patch_tool tools/rt64/src/contrib/plume switch/patches/plume-switch.patch
patch_tool tools/RecompFrontend recomp/recompfrontend.patch switch/patches/recompfrontend-switch.patch
RMLUI=tools/RecompFrontend/recompui/lib/RmlUi
git -C "$RMLUI" apply --reverse --check "$ROOT/recomp/rmlui.patch" 2>/dev/null || \
    git -C "$RMLUI" apply "$ROOT/recomp/rmlui.patch" || fail "couldn't apply recomp/rmlui.patch."

# ---------------------------------------------------------------- the recompiled game
if [ -n "$ROM" ]; then
    # As build.sh does it, without building the PC version.
    step "Recompiling the game from your ROM"
    cp "$ROM" conker/baserom.us.z64
    python3 -c "import sys; sys.path.insert(0, 'recomp'); import unpack_rom; unpack_rom.unpack(open('conker/baserom.us.z64', 'rb').read())"
    if [ ! -f tools/N64Recomp/build/build.ninja ]; then
        cmake -S tools/N64Recomp -B tools/N64Recomp/build -G Ninja -DCMAKE_BUILD_TYPE=Release
    fi
    cmake --build tools/N64Recomp/build --target N64RecompCLI RSPRecomp
    python3 recomp/recompile.py
fi

# ---------------------------------------------------------------- build tools
# RT64 turns its shaders into C with file_to_c during the build, so it has to run here.
step "Building file_to_c for this machine"
TOOLS=host/build-switch-tools
mkdir -p "$TOOLS"
c++ -O2 -std=c++17 -o "$TOOLS/file_to_c" tools/rt64/src/tools/file_to_c/file_to_c.cpp

# ---------------------------------------------------------------- the game
step "Building the game for the Switch (in $BUILD)"
cmake -S host -B "$BUILD" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/Switch.cmake" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCONKER_HOST_ONLY=$HOST_ONLY \
    -DRT64_HOST_FILE_TO_C="$ROOT/$TOOLS/file_to_c" \
    ${NVK_SDK:+-DCONKER_NVK_SDK="$NVK_SDK"}
cmake --build "$BUILD" -j"$JOBS"

if [ "$HOST_ONLY" = ON ]; then
    printf '\nDone: everything but the game compiles (nothing was linked).\n'
else
    printf '\nDone: %s/ConkerRecomp.nro\n' "$(cd "$BUILD" && pwd)"
    printf 'Copy it with host/assets/ into sdmc:/switch/cbfd/ (switch/deploy.sh does), and your ROM as sdmc:/switch/cbfd/conker.z64.\n'
fi
