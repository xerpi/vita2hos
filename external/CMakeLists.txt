# Use Vita3K's glslang (installed glslang doesn't contain SpvBuilder.h, etc)
set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(Vita3K/external/glslang)

# Shim libraries for Vita3K
add_library(vita3k_shim INTERFACE)
target_include_directories(vita3k_shim INTERFACE ${PROJECT_SOURCE_DIR}/include/vita3k)

add_library(util INTERFACE)
target_link_libraries(util INTERFACE vita3k_shim)
target_include_directories(util INTERFACE Vita3K/vita3k/util/include)

add_library(features INTERFACE)
target_link_libraries(features INTERFACE vita3k_shim)
target_include_directories(features INTERFACE Vita3K/vita3k/features/include)

# Vita3K gxm
add_subdirectory(Vita3K/vita3k/gxm)
target_link_libraries(gxm INTERFACE vita3k_shim)
target_include_directories(gxm PRIVATE
    ${PROJECT_SOURCE_DIR}/include/vita3k
)

# Vita3K shader
add_subdirectory(Vita3K/vita3k/shader)
target_include_directories(shader PRIVATE
    ${NX_ROOT}/include/spirv_cross
    ${NX_ROOT}/include/glslang
)

# M*LIB
add_library(mlib INTERFACE)
target_include_directories(mlib INTERFACE mlib)