cmake_minimum_required(VERSION 3.20)
# vktest.nro: NVK clear-screen check (switch/build.sh --vktest).
project(vktest C)

set(CONKER_NVK_SDK "" CACHE PATH "mesa-switch install prefix, with lib/libvulkan.a")
if (NOT EXISTS "${CONKER_NVK_SDK}/lib/libvulkan.a")
    message(FATAL_ERROR "Set CONKER_NVK_SDK to mesa-switch's install prefix.")
endif()

add_executable(vktest vktest.c)
target_include_directories(vktest PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/rt64/src/contrib/plume/contrib/Vulkan-Headers/include")
target_link_libraries(vktest PRIVATE "-Wl,-u,vk_icdGetInstanceProcAddr" "${CONKER_NVK_SDK}/lib/libvulkan.a"
    expat zstd z nx m stdc++)
nx_generate_nacp(vktest.nacp NAME "CBFD vktest" AUTHOR "CBFD-Recompiled" VERSION "0.1")
nx_create_nro(vktest NACP vktest.nacp ICON "${CMAKE_CURRENT_SOURCE_DIR}/../icon.jpg")
