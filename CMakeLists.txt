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
set(NX_ROOT ${DEVKITPRO}/libnx32)

include(Platform/NintendoSwitch)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(VITASDK_INCLUDE ${CMAKE_SOURCE_DIR}/vita-headers/include)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${NX_ARCH_SETTINGS} ${NX_COMMON_FLAGS} -idirafter ${VITASDK_INCLUDE}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${NX_ARCH_SETTINGS} ${NX_COMMON_FLAGS} -idirafter ${VITASDK_INCLUDE}")

add_executable(vita2hos
    source/load.c
    source/log.c
    source/main.c
    source/module.c
    source/uam_compiler_iface_c.cpp
    source/util.c
    source/vita3k_shader_recompiler_iface_c.cpp
    source/modules/SceCtrl.c
    source/modules/SceDisplay.c
    source/modules/SceGxm.c
    source/modules/SceKernelThreadMgr.c
    source/modules/SceLibKernel.c
    source/modules/SceSysmem.c
    source/modules/SceTouch.c
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
)

target_include_directories(vita2hos PRIVATE
    include
    include/vita3k
    Vita3K/vita3k/gxm/include
    Vita3K/vita3k/shader/include
    Vita3K/vita3k/features/include
    Vita3K/vita3k/util/include
    ${NX_ROOT}/include/spirv_cross
    ${NX_ROOT}/include/glslang
    ${NX_ROOT}/include/uam/mesa-imported
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
    SPIRV
    spirv-cross-c
    spirv-cross-core
    spirv-cross-cpp
    spirv-cross-glsl
    spirv-cross-reflect
    spirv-cross-util
    glslang
    fmt
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
