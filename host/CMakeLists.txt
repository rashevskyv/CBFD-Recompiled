cmake_minimum_required(VERSION 3.20)

# Host application for the N64Recomp output of Conker's Bad Fur Day (US).
# RecompiledFuncs/ must exist first (`python recomp/recompile.py`; build.cmd / build.sh run it).
#
# Windows (RT64 window, keyboard and controllers) with MSVC:
#   host\build_windows.cmd   (sets up MSVC, then configures and builds host/build-win)
# Linux/WSL and macOS (Metal; use Apple's clang, /usr/bin/clang):
#   cmake -S host -B host/build -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=RelWithDebInfo
#   cmake --build host/build

project(ConkerRecomp C CXX)
if (APPLE)
    # RT64 and RecompFrontend have Objective-C++ sources (Metal, Cocoa); rt64 enables
    # the languages only in its own directory.
    enable_language(OBJC OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)
# MSVC compiles C as C89 unless told otherwise; o1heap needs C99.
set(CMAKE_C_STANDARD 11)

# OFF builds a headless host (null renderer, no input or sound), for tests.
option(CONKER_RT64 "Render with RT64 in an SDL window" ON)
# ON compiles the host, the runtime and the renderer without the recompiled game (so without
# the ROM), and links nothing: a check that everything else builds, as for a new platform.
option(CONKER_HOST_ONLY "Compile everything but the recompiled game, without linking" OFF)
# Nintendo Switch (libnx), configured with $DEVKITPRO/cmake/Switch.cmake: see switch/build.sh.
if (NINTENDO_SWITCH)
    set(CONKER_SWITCH ON)
else()
    set(CONKER_SWITCH OFF)
endif()

set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(RT64_DIR "${REPO_ROOT}/tools/rt64")

if (CONKER_SWITCH)
    # newlib only declares POSIX (posix_memalign, semaphores, ...) for strict -std=c++20
    # when asked to.
    add_compile_definitions(_GNU_SOURCE)
    # What newlib lacks (timegm), everywhere.
    add_compile_options("SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/switch/include/switch_compat.h")
endif()

add_subdirectory("${REPO_ROOT}/tools/N64ModernRuntime" "${CMAKE_CURRENT_BINARY_DIR}/N64ModernRuntime")
if (CONKER_SWITCH)
    # sljit (the mods' recompiler) can't use its mmap allocator on Horizon.
    target_compile_options(LiveRecomp PRIVATE "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/switch/nx_sljit_config.h")
endif()
if (MSVC)
    # MSVC 14.51 hits an internal compiler error optimising librecomp's flash.cpp.
    set_source_files_properties("${REPO_ROOT}/tools/N64ModernRuntime/librecomp/src/flash.cpp"
        DIRECTORY "${REPO_ROOT}/tools/N64ModernRuntime/librecomp"
        PROPERTIES COMPILE_OPTIONS "/Od")
endif()

if (NOT CONKER_HOST_ONLY)
    # The recompiled game code. recomp/recompile.py records hashes of the files that shape it; stop if
    # any has changed since, as the build would otherwise quietly use stale code.
    set(RECOMP_INPUTS conker.toml recomp/conker.us.syms.toml recomp/code_rewrites.txt recomp/n64recomp.patch recomp/unpack_rom.py recomp/emit_tlb_pages.py recomp/audio_ucode.toml)
    set(RECOMP_STAMP "${REPO_ROOT}/RecompiledFuncs/inputs.sha256")
    if (NOT EXISTS "${RECOMP_STAMP}")
        message(FATAL_ERROR "RecompiledFuncs/ is missing or predates input tracking: run build.cmd / build.sh, or `python recomp/recompile.py`.")
    endif()
    file(READ "${RECOMP_STAMP}" RECOMP_STAMP_TEXT)
    set(RECOMP_EXPECTED "")
    foreach(input ${RECOMP_INPUTS})
        file(READ "${REPO_ROOT}/${input}" input_text)
        string(REPLACE "\r" "" input_text "${input_text}")
        string(SHA256 input_hash "${input_text}")
        string(APPEND RECOMP_EXPECTED "${input_hash} ${input}\n")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${REPO_ROOT}/${input}")
    endforeach()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${RECOMP_STAMP}")
    if (NOT RECOMP_STAMP_TEXT STREQUAL RECOMP_EXPECTED)
        message(FATAL_ERROR "RecompiledFuncs/ is out of date (${RECOMP_INPUTS} changed since it was generated): run build.cmd / build.sh, or `python recomp/recompile.py`, then build again.")
    endif()
    file(GLOB RECOMPILED_FUNCS CONFIGURE_DEPENDS "${REPO_ROOT}/RecompiledFuncs/funcs_*.c")
    add_library(RecompiledFuncs STATIC ${RECOMPILED_FUNCS} "${REPO_ROOT}/RecompiledFuncs/tlb_pages.c")
    target_include_directories(RecompiledFuncs PRIVATE
        "${REPO_ROOT}/tools/N64ModernRuntime/N64Recomp/include"
        "${REPO_ROOT}/RecompiledFuncs"
    )
    # funcs.h does not declare the libultra functions N64Recomp skips (the runtime
    # provides them), and the generated code has many unused labels and variables.
    # setjmp.h is for conker.toml's hooks on the script interpreter's abort (func_150AE280).
    if (MSVC)
        target_compile_options(RecompiledFuncs PRIVATE /W0 /bigobj /FIsetjmp.h)
    else()
        target_compile_options(RecompiledFuncs PRIVATE
            -include setjmp.h
            -Wno-implicit-function-declaration
            -Wno-unused-label
            -Wno-unused-variable
            -Wno-unused-but-set-variable
            -fno-strict-aliasing
        )
    endif()
endif()

if (CONKER_HOST_ONLY)
    add_library(ConkerRecomp OBJECT)
    # Stand-ins for the recompiler's generated headers.
    target_include_directories(ConkerRecomp PRIVATE host_only)
else()
    add_executable(ConkerRecomp
        # The audio microcode, recompiled by RSPRecomp (recomp/audio_ucode.toml, run by recomp/recompile.py).
        "${REPO_ROOT}/RecompiledFuncs/rsp/audio_ucode.cpp"
    )
    target_link_libraries(ConkerRecomp PRIVATE RecompiledFuncs)
endif()
target_sources(ConkerRecomp PRIVATE
    src/main.cpp
    src/null_renderer.cpp
    src/overlays.cpp
    src/ultra_extras.cpp
    src/mod_api.cpp
    src/rom_versions.cpp
    src/widescreen.cpp
    src/cutscene_aspect.cpp
    src/field_of_view.cpp
    src/interpolation.cpp
    src/model_detail.cpp
    src/render_fixes.cpp
    src/look_aim.cpp
    src/mouse_camera.cpp
)
if (CONKER_SWITCH)
    target_sources(ConkerRecomp PRIVATE switch/switch_nx.cpp switch/switch_compat.c switch/nx_sljit.c)
endif()
if (MSVC)
    set_source_files_properties("${REPO_ROOT}/RecompiledFuncs/rsp/audio_ucode.cpp" PROPERTIES COMPILE_OPTIONS "/W0;/bigobj")
endif()
target_include_directories(ConkerRecomp PRIVATE
    "${REPO_ROOT}/tools/N64ModernRuntime/N64Recomp/include"
    "${REPO_ROOT}/RecompiledFuncs"
)
target_link_libraries(ConkerRecomp PRIVATE librecomp ultramodern)

if (CONKER_RT64)
    set(RT64_STATIC ON CACHE BOOL "" FORCE)
    # RT64 only needs zstd's library. Its programs also copy man pages with a shell
    # command that breaks on a path with an apostrophe.
    set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
    if (CONKER_SWITCH)
        # An SDL window (switch-sdl2) for the screen and input, as on Linux; RT64 presents through
        # Mesa's NVK, linked in, onto libnx's window (plume's __SWITCH__ code).
        set(RT64_SDL_WINDOW_VULKAN ON CACHE BOOL "" FORCE)
        add_compile_definitions(PLUME_SDL_VULKAN_ENABLED RT64_SDL_WINDOW_VULKAN)
        # No file dialogs: RT64 and the launcher link this stand-in.
        add_library(nfd STATIC switch/nfd_stub.c)
        target_include_directories(nfd PUBLIC "${RT64_DIR}/src/contrib/nativefiledialog-extended/src/include")
        # volk's function pointers are named like the Vulkan functions, which the driver linked in
        # also defines: rename volk's, everywhere.
        set(VOLK_H "${RT64_DIR}/src/contrib/plume/contrib/volk/volk.h")
        file(STRINGS "${VOLK_H}" volk_pointers REGEX "^extern PFN_vk[A-Za-z0-9_]+ vk[A-Za-z0-9_]+;")
        set(volk_aliases "// Generated by host/CMakeLists.txt from volk.h (Switch build).\n#pragma once\n")
        foreach(line ${volk_pointers})
            string(REGEX REPLACE "^extern PFN_vk[A-Za-z0-9_]+ (vk[A-Za-z0-9_]+);" "\\1" name "${line}")
            string(APPEND volk_aliases "#define ${name} volk_${name}\n")
        endforeach()
        set(VOLK_SWITCH_ALIASES "${CMAKE_CURRENT_BINARY_DIR}/volk_switch_aliases.h")
        file(CONFIGURE OUTPUT "${VOLK_SWITCH_ALIASES}" CONTENT "${volk_aliases}" @ONLY)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${VOLK_H}")
        add_compile_options("SHELL:-include ${VOLK_SWITCH_ALIASES}")
        target_compile_options(ConkerRecomp PRIVATE "SHELL:-include ${VOLK_SWITCH_ALIASES}")
    elseif (NOT WIN32 AND NOT APPLE)
        # On Linux, RT64 renders with Vulkan into an SDL window (as in
        # Banjo: Recompiled): the window is created with SDL_WINDOW_VULKAN.
        # On macOS it renders with Metal into the window's CAMetalLayer.
        set(RT64_SDL_WINDOW_VULKAN ON CACHE BOOL "" FORCE)
        add_compile_definitions(PLUME_SDL_VULKAN_ENABLED RT64_SDL_WINDOW_VULKAN)
        # The file dialogs (Load ROM, mods) ask the desktop for its own through
        # xdg-desktop-portal (D-Bus) instead of opening a GTK one in the game: GTK
        # crashed building its dialog, which the launcher opens from its own thread,
        # not the one GTK started on.
        set(NFD_PORTAL ON CACHE BOOL "" FORCE)
    endif()
    add_subdirectory("${RT64_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/rt64")
    if (CONKER_SWITCH)
        # volk's loader includes dlfcn.h, which newlib doesn't have (it's never used: see plume).
        target_include_directories(plume PRIVATE switch/include)
        # A command-line tool, for the build machine.
        set_target_properties(re-spirv-cli PROPERTIES EXCLUDE_FROM_ALL TRUE)
    endif()

    # RecompFrontend: the launcher, config and mod menus (recompui, on RmlUi) and
    # remappable input (recompinput). tools/RecompFrontend is an untracked checkout.
    if (WIN32)
        set(sdl2_SOURCE_DIR "${RT64_DIR}/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3")
    else()
        # The system SDL2; RecompFrontend takes its headers from SDL2_INCLUDE_DIRS.
        find_package(SDL2 REQUIRED)
    endif()
    set(RECOMP_FRONTEND_N64MODERNRUNTIME_PATH "${REPO_ROOT}/tools/N64ModernRuntime")
    set(RECOMP_FRONTEND_RT64_PATH "${RT64_DIR}")
    # recompui compiles its shaders with rt64's build_*_shader functions, which use
    # these variables from rt64's own directory scope: repeat them here.
    set(DXC_COMMON_OPTS "-I${RT64_DIR}/src")
    set(DXC_DXIL_OPTS "-Wno-ignored-attributes")
    set(DXC_SPV_OPTS "-spirv" "-fspv-target-env=vulkan1.0" "-fvk-use-dx-layout")
    set(DXC_PS_OPTS "${DXC_COMMON_OPTS}" "-E" "PSMain" "-T ps_6_0" "-D DYNAMIC_RENDER_PARAMS")
    set(DXC_VS_OPTS "${DXC_COMMON_OPTS}" "-E" "VSMain" "-T vs_6_0" "-D DYNAMIC_RENDER_PARAMS" "-fvk-invert-y")
    # DXC runs on the machine doing the build, which is not the target when cross-compiling.
    if (CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        set(DXC_ARCH x64)
    else()
        set(DXC_ARCH arm64)
    endif()
    if (WIN32)
        set(DXC "${RT64_DIR}/src/contrib/dxc/bin/x64/dxc.exe")
    elseif (APPLE)
        # recompui's shaders also go through spirv_cross_msl (rt64's target) and xcrun metal.
        set(DXC "DYLD_LIBRARY_PATH=${RT64_DIR}/src/contrib/dxc/lib/${DXC_ARCH}" "${RT64_DIR}/src/contrib/dxc/bin/${DXC_ARCH}/dxc-macos")
    else()
        set(DXC "LD_LIBRARY_PATH=${RT64_DIR}/src/contrib/dxc/lib/${DXC_ARCH}" "${RT64_DIR}/src/contrib/dxc/bin/${DXC_ARCH}/dxc-linux")
    endif()
    add_subdirectory("${REPO_ROOT}/tools/RecompFrontend" "${CMAKE_CURRENT_BINARY_DIR}/RecompFrontend")
    if (CONKER_SWITCH)
        # lunasvg's loading of font files into its font cache uses mmap: leave it out.
        target_compile_definitions(plutovg PRIVATE PLUTOVG_DISABLE_FONT_FACE_CACHE_LOAD)
    endif()
    if (MSVC)
        # Their GCC-style -Wno-unused-parameter is an error for cl.
        set_property(TARGET recompui recompinput PROPERTY COMPILE_OPTIONS "")
    endif()
    target_link_libraries(ConkerRecomp PRIVATE recompui recompinput nfd)
    if (NOT CONKER_HOST_ONLY)
        # recompui loads its fonts, icons and style sheet from assets/ next to the executable.
        add_custom_command(TARGET ConkerRecomp POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:ConkerRecomp>/assets")
    endif()

    target_sources(ConkerRecomp PRIVATE src/frontend.cpp src/conker_config.cpp src/audio_output.cpp src/rumble.cpp src/fps_counter.cpp src/reticle.cpp src/pad_mappings.cpp src/texture_packs.cpp src/gliden64_packs.cpp src/notices.cpp src/local_achievements.cpp src/save_states.cpp)

    # CONKER_RT64 selects the window build; the rest are what rt64 compiles its own headers with.
    target_compile_definitions(ConkerRecomp PRIVATE CONKER_RT64 HLSL_CPU)
    if (WIN32)
        target_compile_definitions(ConkerRecomp PRIVATE NOMINMAX)
    endif()
    # rt64 sets its include paths per directory, so repeat the ones its headers need.
    target_include_directories(ConkerRecomp PRIVATE
        "${RT64_DIR}/src"
        "${RT64_DIR}/src/contrib"
        "${RT64_DIR}/src/contrib/plume"
        "${RT64_DIR}/src/contrib/imgui"
        "${RT64_DIR}/src/contrib/hlslpp/include"
        "${RT64_DIR}/src/contrib/mupen64plus-core/src/api"
        "${RT64_DIR}/src/contrib/nativefiledialog-extended/src/include"
    )
    target_link_libraries(ConkerRecomp PRIVATE rt64)

    if (WIN32)
        set(SDL2_DIR "${RT64_DIR}/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3")
        target_include_directories(ConkerRecomp PRIVATE "${SDL2_DIR}/include" "${RT64_DIR}/src/contrib/dxc/inc")
        target_link_directories(ConkerRecomp PRIVATE "${SDL2_DIR}/lib/x64")
        target_link_libraries(ConkerRecomp PRIVATE SDL2)
        if (NOT CONKER_HOST_ONLY)
            # Runtime DLLs next to the executable: SDL2, and DXC which RT64 loads at startup.
            add_custom_command(TARGET ConkerRecomp POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${SDL2_DIR}/lib/x64/SDL2.dll"
                    "${RT64_DIR}/src/contrib/dxc/bin/x64/dxcompiler.dll"
                    "${RT64_DIR}/src/contrib/dxc/bin/x64/dxil.dll"
                    "$<TARGET_FILE_DIR:ConkerRecomp>")
        endif()
    else()
        target_include_directories(ConkerRecomp PRIVATE ${SDL2_INCLUDE_DIRS})
        target_link_libraries(ConkerRecomp PRIVATE ${SDL2_LIBRARIES})
    endif()
endif()

if (NOT MSVC AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    # The recompiled audio microcode's vector unit uses SSSE3 and SSE4.1 intrinsics,
    # which GCC and Clang only allow when targeting them (MSVC always does).
    target_compile_options(ConkerRecomp PRIVATE -msse4.1)
endif()

if (UNIX AND NOT CONKER_SWITCH)
    # Exported symbols let the crash handler's backtrace name recompiled functions.
    target_link_options(ConkerRecomp PRIVATE -rdynamic)
elseif (WIN32)
    # The crash handler symbolises the stack with DbgHelp.
    target_link_libraries(ConkerRecomp PRIVATE DbgHelp)
    # Mods replace recompiled functions by patching their machine code, so identical
    # functions must not be folded into one.
    target_link_options(ConkerRecomp PRIVATE /OPT:NOICF)
endif()
if (APPLE)
    # Mods patch the recompiled functions' machine code, which needs __TEXT's maximum protection
    # to allow writing; Apple's linker caps it at r-x, so a wrapper raises it after linking
    # (host/macos/ld64; without it macOS killed the game as soon as a mod was enabled, issue #81).
    target_link_options(ConkerRecomp PRIVATE "-fuse-ld=${CMAKE_CURRENT_SOURCE_DIR}/macos/ld64")
endif()
if (CONKER_SWITCH AND NOT CONKER_HOST_ONLY)
    # The NRO. Not linked yet (needs RecompiledFuncs/ and an NVK build): see CLAUDE.md, Findings.
    if (CONKER_RT64)
        # Mesa's NVK for the Switch (danfromtico/mesa-switch), installed with its EGL and glapi:
        # these replace switch-sdl2's old EGL/glapi/drm_nouveau (switch-mesa 20.1), which must not
        # be linked next to it.
        set(CONKER_NVK_SDK "" CACHE PATH "mesa-switch install prefix, with lib/libvulkan.a")
        if (NOT EXISTS "${CONKER_NVK_SDK}/lib/libvulkan.a")
            message(FATAL_ERROR "The Switch build needs Mesa's NVK: set NVK_SDK (CONKER_NVK_SDK) to mesa-switch's install prefix.")
        endif()
        foreach(sdl_target SDL2::SDL2 SDL2::SDL2-static)
            get_target_property(sdl_aliased ${sdl_target} ALIASED_TARGET)
            if (TARGET ${sdl_target} AND NOT sdl_aliased)
                get_target_property(sdl_link_libraries ${sdl_target} INTERFACE_LINK_LIBRARIES)
                if (sdl_link_libraries)
                    list(FILTER sdl_link_libraries EXCLUDE REGEX "^(-l)?(EGL|glapi|drm_nouveau)$")
                    set_target_properties(${sdl_target} PROPERTIES INTERFACE_LINK_LIBRARIES "${sdl_link_libraries}")
                endif()
            endif()
        endforeach()
        target_link_libraries(ConkerRecomp PRIVATE
            "${CONKER_NVK_SDK}/lib/libEGL.a" "${CONKER_NVK_SDK}/lib/libglapi.a"
            # The driver's entry point (plume's volkInitializeCustom); not --whole-archive, which
            # brings in duplicate entry points.
            "-Wl,-u,vk_icdGetInstanceProcAddr" "${CONKER_NVK_SDK}/lib/libvulkan.a"
            expat zstd z nx m stdc++)
    endif()
    nx_generate_nacp(ConkerRecomp.nacp NAME "Conker's Bad Fur Day: Recompiled" AUTHOR "CBFD-Recompiled" VERSION "0.1.6")
    nx_create_nro(ConkerRecomp NACP ConkerRecomp.nacp)
endif()
