#!/bin/sh
# Builds Conker's Bad Fur Day: Recompiled for the Nintendo Switch (a libnx NRO), on Linux:
#   switch/build.sh [--host-only]
# Needs devkitPro (devkitA64, libnx, switch-sdl2, switch-freetype, switch-libzstd) in $DEVKITPRO
# (default /opt/devkitpro); the devkitpro/devkita64 Docker image has all of them.
#   --host-only  compile everything but the recompiled game, and link nothing (no ROM needed)
# The game itself needs RecompiledFuncs/ (made from your ROM by ./build.sh, or
# `python3 recomp/recompile.py`), and Mesa's NVK for the Switch in $NVK_SDK (see CLAUDE.md).
set -e

cd "$(dirname "$0")/.."
ROOT=$(pwd)
HOST_ONLY=OFF
for arg in "$@"; do
    case "$arg" in
        --host-only) HOST_ONLY=ON ;;
        -h|--help) sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "Unknown option: $arg" >&2; exit 1 ;;
    esac
done
JOBS=$(nproc 2>/dev/null || echo 4)
: "${DEVKITPRO:=/opt/devkitpro}"
export DEVKITPRO

step() { printf '\n==> %s\n' "$*"; }
fail() { printf '\nError: %s\n' "$*" >&2; exit 1; }

[ -f "$DEVKITPRO/cmake/Switch.cmake" ] || fail "no devkitPro in $DEVKITPRO (set DEVKITPRO)."
for cmd in git cmake ninja c++; do
    command -v "$cmd" >/dev/null 2>&1 || fail "$cmd is missing."
done
if [ "$HOST_ONLY" = OFF ] && [ ! -f RecompiledFuncs/inputs.sha256 ]; then
    fail "RecompiledFuncs/ is missing: run ./build.sh with your ROM first, or use --host-only."
fi

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
step "Getting the submodules"
git submodule update --init --recursive
step "Patching the tools"
patch_tool tools/N64ModernRuntime recomp/n64modernruntime.patch switch/patches/n64modernruntime-switch.patch
patch_tool tools/rt64 recomp/rt64.patch switch/patches/rt64-switch.patch
patch_tool tools/rt64/src/contrib/plume switch/patches/plume-switch.patch
patch_tool tools/RecompFrontend recomp/recompfrontend.patch switch/patches/recompfrontend-switch.patch
RMLUI=tools/RecompFrontend/recompui/lib/RmlUi
git -C "$RMLUI" apply --reverse --check "$ROOT/recomp/rmlui.patch" 2>/dev/null || \
    git -C "$RMLUI" apply "$ROOT/recomp/rmlui.patch" || fail "couldn't apply recomp/rmlui.patch."

# ---------------------------------------------------------------- build tools
# RT64 turns its shaders into C with file_to_c during the build, so it has to run here.
step "Building file_to_c for this machine"
TOOLS=host/build-switch-tools
mkdir -p "$TOOLS"
c++ -O2 -std=c++17 -o "$TOOLS/file_to_c" tools/rt64/src/tools/file_to_c/file_to_c.cpp

# ---------------------------------------------------------------- the game
step "Building the game for the Switch"
BUILD=host/build-switch
if [ "$HOST_ONLY" = ON ]; then
    BUILD=host/build-switch-host-only
fi
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
    printf '\nDone: %s/%s/ConkerRecomp.nro\n' "$ROOT" "$BUILD"
    printf 'Copy it with host/assets/ into sdmc:/switch/cbfd/, and your ROM as sdmc:/switch/cbfd/conker.z64.\n'
fi
