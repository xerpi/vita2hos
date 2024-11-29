cmake_minimum_required(VERSION 3.13)

set(VITA2HOS_MAJOR 0)
set(VITA2HOS_MINOR 3)
set(VITA2HOS_PATCH 0)
execute_process(COMMAND
    git describe --dirty --always --exclude '*'
    OUTPUT_VARIABLE VITA2HOS_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

project(
    vita2hos
    VERSION ${VITA2HOS_MAJOR}.${VITA2HOS_MINOR}.${VITA2HOS_PATCH}
    LANGUAGES C CXX
)

# Overwrite libnx location
set(NX_ROOT ${DEVKITPRO}/libnx)

include(Platform/NintendoSwitch)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set Vulkan paths
set(Vulkan_INCLUDE_DIR "/usr/include/vulkan")
set(Vulkan_LIBRARY "/usr/lib/x86_64-linux-gnu/libvulkan.so")

# Find Vulkan package
find_package(Vulkan REQUIRED)

if(DEFINED ENV{VITASDK})
    set(VITASDK $ENV{VITASDK})
    set(VITASDK_INCLUDE ${VITASDK}/arm-vita-eabi/include)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${NX_ARCH_SETTINGS} ${NX_COMMON_FLAGS} -idirafter ${VITASDK_INCLUDE}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${NX_ARCH_SETTINGS} ${NX_COMMON_FLAGS} -idirafter ${VITASDK_INCLUDE}")
else()
    message(WARNING "VITASDK not found, using only Switch toolchain")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${NX_ARCH_SETTINGS} ${NX_COMMON_FLAGS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${NX_ARCH_SETTINGS} ${NX_COMMON_FLAGS}")
endif()

add_executable(vita2hos
    source/load.c
    source/log.c
    source/main.c
    source/module.c
    source/mutex.c
    source/uam_compiler_iface_c.cpp
    source/util.c
    source/vita3k_shader_recompiler_iface_c.cpp
    source/modules/SceCtrl.c
    source/modules/SceDisplay.c
    source/modules/SceGxm.c
    source/modules/SceGxm_cpp.cpp
    source/modules/SceKernelThreadMgr.c
    source/modules/SceLibKernel.c
    source/modules/SceSysmem.c
    source/modules/SceTouch.c
    src/uam/gxm/color_surface.cpp
    Vita3K/vita3k/gxm/src/attributes.cpp
    Vita3K/vita3k/gxm/src/color.cpp
    Vita3K/vita3k/gxm/src/gxp.cpp
    Vita3K/vita3k/gxm/src/stream.cpp
    Vita3K/vita3k/gxm/src/textures.cpp
    Vita3K/vita3k/gxm/src/transfer.cpp
    Vita3K/vita3k/shader/src/gxp_parser.cpp
    Vita3K/vita3k/shader/src/spirv_recompiler.cpp
    Vita3K/vita3k/shader/src/translator/alu.cpp
    Vita3K/vita3k/shader/src/translator/branch_cond.cpp
    Vita3K/vita3k/shader/src/translator/data.cpp
    Vita3K/vita3k/shader/src/translator/ialu.cpp
    Vita3K/vita3k/shader/src/translator/illegal.cpp
    Vita3K/vita3k/shader/src/translator/special.cpp
    Vita3K/vita3k/shader/src/translator/texture.cpp
    Vita3K/vita3k/shader/src/translator/utils.cpp
    Vita3K/vita3k/shader/src/usse_decode_helpers.cpp
    Vita3K/vita3k/shader/src/usse_disasm.cpp
    Vita3K/vita3k/shader/src/usse_program_analyzer.cpp
    Vita3K/vita3k/shader/src/usse_translator_entry.cpp
    Vita3K/vita3k/shader/src/usse_utilities.cpp
    Vita3K/external/miniz/miniz.c
)

# Добавляем флаги для предотвращения конфликтов заголовочных файлов
add_compile_options(-include ${CMAKE_SOURCE_DIR}/include/compiler_types.h)
add_compile_options(-include ${CMAKE_SOURCE_DIR}/include/compiler_compat.h)

# Добавляем флаги для использования только заголовков devkitA64
add_compile_options(-nostdinc)
add_compile_options(-isystem /opt/devkitpro/devkitA64/aarch64-none-elf/include)
add_compile_options(-isystem /opt/devkitpro/devkitA64/lib/gcc/aarch64-none-elf/14.2.0/include)
add_compile_options(-isystem /opt/devkitpro/libnx/include)
add_compile_options(-isystem /usr/local/vitasdk/arm-vita-eabi/include)  # Добавляем путь к PSP2 заголовкам
add_compile_options(-isystem /usr/include)  # Для системных заголовков
add_compile_options(-isystem /usr/include/vulkan)  # Для Vulkan

# Добавляем наши заголовочные файлы
include_directories(${CMAKE_SOURCE_DIR}/include)

target_include_directories(vita2hos PRIVATE
    include/vita3k
    include/uam
    Vita3K/vita3k
    Vita3K/vita3k/gxm/include
    Vita3K/vita3k/shader/include
    Vita3K/vita3k/features/include
    Vita3K/vita3k/util/include
    Vita3K/external
    Vita3K/external/miniz
    ${NX_ROOT}/include/spirv_cross
    ${NX_ROOT}/include/glslang
    ${NX_ROOT}/include/uam/mesa-imported
    ${NX_ROOT}/include/uam
    ${Vulkan_INCLUDE_DIR}
    /usr/local/vitasdk/arm-vita-eabi/include
    /opt/devkitpro/devkitA64/aarch64-none-elf/include
    /opt/devkitpro/devkitA64/lib/gcc/aarch64-none-elf/14.2.0/include
    /opt/devkitpro/libnx/include
)

target_compile_options(vita2hos PRIVATE
    -Wall
)

target_compile_definitions(vita2hos PRIVATE
    VITA2HOS_MAJOR="${VITA2HOS_MAJOR}"
    VITA2HOS_MINOR="${VITA2HOS_MINOR}"
    VITA2HOS_PATCH="${VITA2HOS_PATCH}"
    VITA2HOS_HASH="${VITA2HOS_HASH}"
)

target_link_libraries(vita2hos PRIVATE
    ${Vulkan_LIBRARIES}
    spirv-cross-core
    spirv-cross-glsl
    spirv-cross-hlsl
    spirv-cross-cpp
    spirv-cross-reflect
    spirv-cross-util
    glslang
    uam
    miniz
    deko3dd
)

target_link_directories(vita2hos PRIVATE
    ${NX_ROOT}/lib
)

target_link_options(vita2hos PRIVATE
    -specs=${NX_ROOT}/switch32.specs
    -Wl,-Map,${CMAKE_PROJECT_NAME}.map
    # pthread_cancel gets removed otherwise, which makes __ghtread_active_p() fail
    -Wl,-u,pthread_cancel
)

find_program(NX_ELF2NSO_EXE NAMES elf2nso HINTS "${DEVKITPRO}/tools/bin")
find_program(NX_NPDMTOOL_EXE NAMES npdmtool HINTS "${DEVKITPRO}/tools/bin")
find_program(NX_BUILD_PFS0_EXE NAMES build_pfs0 HINTS "${DEVKITPRO}/tools/bin")

nx_create_exefs(${CMAKE_PROJECT_NAME}
    CONFIG ${CMAKE_PROJECT_NAME}.json
)
