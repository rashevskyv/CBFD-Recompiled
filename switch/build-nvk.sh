#!/bin/bash
# Builds Mesa's NVK for the Switch (danfromtico/mesa-switch, Vulkan only) on an Ubuntu 24.04 x86-64
# machine, without mesa-switch's Docker image, and installs lib/libvulkan.a into $NVK_SDK
# (default /opt/nvk-sdk) for switch/build.sh:
#   switch/build-nvk.sh [mesa-switch checkout, default ../mesa-switch]
# Needs devkitPro in $DEVKITPRO, and (once):
#   apt install python3-pip flex bison libclang-dev llvm-15-dev libllvmspirvlib-15-dev libclc-15-dev libclang-15-dev
#   pip3 install --break-system-packages meson mako pyyaml
#   rustup toolchain install nightly --component rust-src rustfmt
#   rustup target add aarch64-unknown-linux-gnu --toolchain nightly
#   cargo install bindgen-cli cbindgen
#   SPIRV-Tools >= 2024.1 (vulkan-sdk-1.3.290.0) in /usr/local
# Writes to /usr/local/libexec (rustc and bindgen wrappers) and $DEVKITPRO/portlibs/switch
# (nouveau headers, empty libdl/librt/libutil), as mesa-switch's own build-switch.sh does.
set -e

MESA_COMMIT=0ff52079ca9e9e837ed27eb95a8d239f439a1864
SRC=${1:-$(cd "$(dirname "$0")/../.." && pwd)/mesa-switch}
: "${DEVKITPRO:=/opt/devkitpro}"
: "${NVK_SDK:=/opt/nvk-sdk}"
export DEVKITPRO

if [ ! -d "$SRC/.git" ]; then
    git clone --depth 50 https://github.com/danfromtico/mesa-switch "$SRC"
fi
git -C "$SRC" checkout -q "$MESA_COMMIT"
cd "$SRC"
. "$HOME/.cargo/env"

mkdir -p /usr/local/libexec
cp bindgen-switch-wrapper.sh /usr/local/libexec/bindgen
cp rustc-switch-wrapper.sh /usr/local/libexec/rustc
cp bindgen-atomic-shim.h /usr/local/libexec/bindgen-atomic-shim.h
chmod +x /usr/local/libexec/bindgen /usr/local/libexec/rustc
cp src/gallium/winsys/nouveau/drm/nouveau.h "$DEVKITPRO/portlibs/switch/include/"
cp src/nouveau/headers/nv_device_info.h "$DEVKITPRO/portlibs/switch/include/"
# mesa_clc looks for Clang's headers under the major version.
[ -e /usr/lib/llvm-15/lib/clang/15 ] || ln -s 15.0.7 /usr/lib/llvm-15/lib/clang/15
# Rust's std asks for -ldl -lrt -lutil: empty archives (mesa-switch provides the symbols).
if [ ! -f "$DEVKITPRO/portlibs/switch/lib/libdl.a" ]; then
    tmp=$(mktemp -d)
    echo "void __mesa_switch_stub_lib(void) {}" > "$tmp/stub.c"
    "$DEVKITPRO/devkitA64/bin/aarch64-none-elf-gcc" -c "$tmp/stub.c" -o "$tmp/stub.o"
    for L in dl rt util; do
        "$DEVKITPRO/devkitA64/bin/aarch64-none-elf-ar" rcs "$DEVKITPRO/portlibs/switch/lib/lib$L.a" "$tmp/stub.o"
    done
fi

# Host tools (mesa_clc, vtn_bindgen2), with LLVM 15 (meson would pick a newer llvm-config).
printf "[binaries]\nllvm-config = '/usr/lib/llvm-15/bin/llvm-config'\n" > native-llvm15.ini
PATH="/usr/lib/llvm-15/bin:$PATH" meson setup builddir-native --wipe --native-file native-llvm15.ini \
    -Dvulkan-drivers= -Dgallium-drivers= -Dshader-cache=true -Dplatforms= -Dglx=disabled -Degl=disabled \
    -Dopengl=false -Dgles1=disabled -Dgles2=disabled -Dtools=[] -Dllvm=enabled -Dmesa-clc=enabled \
    -Dprecomp-compiler=enabled -Dinstall-mesa-clc=true
ninja -C builddir-native src/compiler/clc/mesa_clc src/compiler/spirv/vtn_bindgen2

# The Switch build. Rust code for the console is built for aarch64-unknown-linux-gnu (the wrapper
# adds the target); proc-macros, which run here, use the plain rustc (native file).
export PATH="/usr/local/libexec:$PWD/builddir-native/src/compiler/clc:$PWD/builddir-native/src/compiler/spirv:$PATH"
export MESA_SWITCH_RUST_TARGET=aarch64-unknown-linux-gnu
printf "[binaries]\nrust = '%s'\ncbindgen = '%s'\n" "$HOME/.cargo/bin/rustc" "$HOME/.cargo/bin/cbindgen" > native-cross-tools.ini
meson setup builddir-switch --wipe --cross-file switch_cross_file.txt --native-file native-cross-tools.ini \
    --buildtype=release -Doptimization=1 -Db_lto=false -Db_ndebug=true -Dvulkan-drivers=nouveau \
    -Dgallium-drivers=nouveau -Dshader-cache=true -Dgallium-rusticl=false -Dplatforms=switch -Dglx=disabled \
    -Degl=disabled -Dopengl=false -Dgles1=disabled -Dgles2=disabled -Dllvm=disabled -Dshared-glapi=disabled \
    -Dshared-llvm=disabled -Dmesa-clc=system -Dprecomp-compiler=system -Dcpp_rtti=false
ninja -C builddir-switch src/nouveau/vulkan/libvulkan.a

mkdir -p "$NVK_SDK/lib"
cp builddir-switch/src/nouveau/vulkan/libvulkan.a "$NVK_SDK/lib/"
echo "NVK: $NVK_SDK/lib/libvulkan.a (mesa-switch $MESA_COMMIT)"
