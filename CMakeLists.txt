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

set(VITA_HEADERS ${CMAKE_SOURCE_DIR}/external/vita-headers)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${NX_ARCH_SETTINGS} ${NX_COMMON_FLAGS} -idirafter ${VITA_HEADERS}/include")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${NX_ARCH_SETTINGS} ${NX_COMMON_FLAGS} -idirafter ${VITA_HEADERS}/include")

add_compile_options(
    -Wall
    -Wimplicit-fallthrough=3
    -Wdouble-promotion
    -fstack-usage
    -fdata-sections
    -ffunction-sections
)

add_subdirectory(external)

add_executable(vita2hos
    source/load.c
    source/log.c
    source/main.c
    source/module.c
    source/netlog.c
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
)

target_include_directories(vita2hos PRIVATE
    include
    ${NX_ROOT}/include/uam/mesa-imported
)

target_compile_options(vita2hos PRIVATE
    -Wextra
    -Wno-unused-parameter
)

target_compile_definitions(vita2hos PRIVATE
    VITA2HOS_MAJOR="${VITA2HOS_MAJOR}"
    VITA2HOS_MINOR="${VITA2HOS_MINOR}"
    VITA2HOS_PATCH="${VITA2HOS_PATCH}"
    VITA2HOS_HASH="${VITA2HOS_HASH}"
)

target_link_libraries(vita2hos PRIVATE
    shader
    spirv-cross-core
    fmt
    uam
    miniz
    $<$<CONFIG:Debug>:deko3dd>
    $<$<CONFIG:Release>:deko3d>
)

target_link_directories(vita2hos PRIVATE
    ${NX_ROOT}/lib
)

target_link_options(vita2hos PRIVATE
    -specs=${NX_ROOT}/switch32.specs
    -Wl,-Map,${CMAKE_PROJECT_NAME}.map
)

find_program(NX_ELF2NSO_EXE NAMES elf2nso HINTS "${DEVKITPRO}/tools/bin")
find_program(NX_NPDMTOOL_EXE NAMES npdmtool HINTS "${DEVKITPRO}/tools/bin")
find_program(NX_BUILD_PFS0_EXE NAMES build_pfs0 HINTS "${DEVKITPRO}/tools/bin")

nx_create_exefs(${CMAKE_PROJECT_NAME}
    CONFIG ${CMAKE_PROJECT_NAME}.json
)

add_custom_target(send
    COMMAND curl -T ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.nsp --user switch:switch ftp://$ENV{SWITCHIP}:5000/vita2hos/vita2hos.nsp
    DEPENDS ${CMAKE_PROJECT_NAME}_nsp
)