cmake_minimum_required(VERSION 3.2)

include(CMakeParseArguments)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_VERSION 1)

if(NOT DEFINED ENV{DEVKITPRO})
  message(FATAL_ERROR "Cannot find devkitpro")
endif()

set(DEVKITPRO "$ENV{DEVKITPRO}" CACHE PATH "Path to devkitpro root")

set(CMAKE_SYSTEM_PROCESSOR "armv8-a")
set(CMAKE_C_COMPILER   "${DEVKITPRO}/devkitARM/bin/arm-none-eabi-gcc${TOOL_OS_SUFFIX}"     CACHE PATH "C compiler")
set(CMAKE_CXX_COMPILER "${DEVKITPRO}/devkitARM/bin/arm-none-eabi-g++${TOOL_OS_SUFFIX}"     CACHE PATH "C++ compiler")
set(CMAKE_ASM_COMPILER "${DEVKITPRO}/devkitARM/bin/arm-none-eabi-gcc${TOOL_OS_SUFFIX}"     CACHE PATH "assembler")
set(CMAKE_STRIP        "${DEVKITPRO}/devkitARM/bin/arm-none-eabi-strip${TOOL_OS_SUFFIX}"   CACHE PATH "strip")
set(CMAKE_AR           "${DEVKITPRO}/devkitARM/bin/arm-none-eabi-ar${TOOL_OS_SUFFIX}"      CACHE PATH "archive")
set(CMAKE_LINKER       "${DEVKITPRO}/devkitARM/bin/arm-none-eabi-ld${TOOL_OS_SUFFIX}"      CACHE PATH "linker")
set(CMAKE_NM           "${DEVKITPRO}/devkitARM/bin/arm-none-eabi-nm${TOOL_OS_SUFFIX}"      CACHE PATH "nm")
set(CMAKE_OBJCOPY      "${DEVKITPRO}/devkitARM/bin/arm-none-eabi-objcopy${TOOL_OS_SUFFIX}" CACHE PATH "objcopy")
set(CMAKE_OBJDUMP      "${DEVKITPRO}/devkitARM/bin/arm-none-eabi-objdump${TOOL_OS_SUFFIX}" CACHE PATH "objdump")
set(CMAKE_RANLIB       "${DEVKITPRO}/devkitARM/bin/arm-none-eabi-ranlib${TOOL_OS_SUFFIX}"  CACHE PATH "ranlib")

# Switch specific tools
set(UAM "${DEVKITPRO}/tools/bin/uam${TOOL_OS_SUFFIX}" CACHE PATH "uam")

# cache flags
set(ARCH_FLAGS                "-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIC -nostartfiles")
set(CMAKE_CXX_FLAGS           "${ARCH_FLAGS} -fno-rtti -fno-exceptions" CACHE STRING "c++ flags")
set(CMAKE_C_FLAGS             "${ARCH_FLAGS}"           CACHE STRING "c flags")
set(CMAKE_CXX_FLAGS_RELEASE   "-O3 -DNDEBUG"            CACHE STRING "c++ Release flags")
set(CMAKE_C_FLAGS_RELEASE     "-O3 -DNDEBUG"            CACHE STRING "c Release flags")
set(CMAKE_CXX_FLAGS_DEBUG     "-O0 -g -DDEBUG -D_DEBUG" CACHE STRING "c++ Debug flags")
set(CMAKE_C_FLAGS_DEBUG       "-O0 -g -DDEBUG -D_DEBUG" CACHE STRING "c Debug flags")
set(CMAKE_SHARED_LINKER_FLAGS ""                        CACHE STRING "shared linker flags")
set(CMAKE_MODULE_LINKER_FLAGS ""                        CACHE STRING "module linker flags")
set(CMAKE_EXE_LINKER_FLAGS    "-specs=${DEVKITPRO}/libnx32/switch32.specs" CACHE STRING "executable linker flags")

# where is the target environment
set(CMAKE_FIND_ROOT_PATH "${DEVKITPRO}/devkitARM/bin" "${DEVKITPRO}/devkitARM/arm-none-eabi" "${DEVKITPRO}/libnx32" "${CMAKE_INSTALL_PREFIX}" "${CMAKE_INSTALL_PREFIX}/share" )

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_compile_definitions(__SWITCH__)

include_directories("${DEVKITPRO}/libnx32/include")
link_directories("${DEVKITPRO}/libnx32/lib")

function(switch_create_nso)
  cmake_parse_arguments(PARSED_ARGS "" "NAME;ELF" "" ${ARGN})

  add_custom_command(
    OUTPUT  ${PARSED_ARGS_NAME}.nso
    COMMAND elf2nso ${PARSED_ARGS_ELF} ${PARSED_ARGS_NAME}.nso
    DEPENDS ${PARSED_ARGS_ELF}
    COMMENT "Generating NSO"
  )

  add_custom_target(${PARSED_ARGS_NAME}-nso ALL
    DEPENDS ${PARSED_ARGS_NAME}.nso
  )
endfunction(switch_create_nso)

function(switch_generate_npdm)
  cmake_parse_arguments(PARSED_ARGS "" "NAME;JSON" "" ${ARGN})

  add_custom_command(
    OUTPUT  ${PARSED_ARGS_NAME}.npdm
    COMMAND npdmtool ${PARSED_ARGS_JSON} ${PARSED_ARGS_NAME}.npdm
    DEPENDS ${PARSED_ARGS_JSON}
    COMMENT "Generating NPDM"
  )

  add_custom_target(${PARSED_ARGS_NAME}-npdm ALL
    DEPENDS ${PARSED_ARGS_NAME}.npdm
  )
endfunction(switch_generate_npdm)

function(switch_create_nsp)
  cmake_parse_arguments(PARSED_ARGS "" "NAME;NSO;NPDM" "" ${ARGN})

  add_custom_command(
    OUTPUT  ${PARSED_ARGS_NAME}.nsp
    COMMAND mkdir -p exefs
    COMMAND cp ${PARSED_ARGS_NSO} exefs/main
    COMMAND cp ${PARSED_ARGS_NPDM} exefs/main.npdm
    COMMAND build_pfs0 exefs ${PARSED_ARGS_NAME}.nsp
    DEPENDS ${PARSED_ARGS_NSO} ${PARSED_ARGS_NPDM}
    COMMENT "Generating NSP"
  )

  add_custom_target(${PARSED_ARGS_NAME}-nsp ALL
    DEPENDS ${PARSED_ARGS_NAME}.nsp
  )
endfunction(switch_create_nsp)
