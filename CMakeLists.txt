cmake_minimum_required(VERSION 3.2)

set(CMAKE_TOOLCHAIN_FILE cmake/libnx32.toolchain.cmake)

set(VITA2HOS_MAJOR 0)
set(VITA2HOS_MINOR 1)
set(VITA2HOS_PATCH 0)

set(APP_TITLE vita2hos)
set(APP_AUTHOR xerpi)
set(APP_VERSION $(VITA2HOS_MAJOR).$(VITA2HOS_MINOR).$(VITA2HOS_PATCH))
set(APP_TITLEID 0101000000000010)

project(vita2hos)

find_library(NX_LIBRARY nx)
find_library(DEKO3DD_LIBRARY deko3dd)

execute_process(
	COMMAND git describe --dirty --always --exclude '*'
	OUTPUT_VARIABLE VITA2HOS_HASH
	OUTPUT_STRIP_TRAILING_WHITESPACE
)

add_compile_definitions(
	VITA2HOS_MAJOR=\"${VITA2HOS_MAJOR}\"
	VITA2HOS_MINOR=\"${VITA2HOS_MINOR}\"
	VITA2HOS_PATCH=\"${VITA2HOS_PATCH}\"
	VITA2HOS_HASH=\"${VITA2HOS_HASH}\"
)

include_directories(
	include
	include/modules
)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -idirafter $ENV{VITASDK}/arm-vita-eabi/include")

set(VERTEX_SHADERS
	shader/basic_vsh.glsl
)

set(FRAGMENT_SHADERS
	shader/color_fsh.glsl
)

foreach(shader ${VERTEX_SHADERS})
	get_filename_component(shader_we ${shader} NAME_WE)
	add_custom_command(OUTPUT "${shader_we}.dksh"
		COMMAND ${UAM} -s vert "${PROJECT_SOURCE_DIR}/${shader}"
			       -o "${CMAKE_BINARY_DIR}/${shader_we}.dksh"
		DEPENDS ${shader}
		COMMENT "Compiling vertex shader ${shader} to ${shader_we}.dksh"
	)
	list(APPEND SHADER_DKSH "${shader_we}.dksh")
endforeach()

foreach(shader ${FRAGMENT_SHADERS})
	get_filename_component(shader_we ${shader} NAME_WE)
	add_custom_command(OUTPUT "${shader_we}.dksh"
		COMMAND ${UAM} -s frag "${PROJECT_SOURCE_DIR}/${shader}"
			       -o "${CMAKE_BINARY_DIR}/${shader_we}.dksh"
		DEPENDS ${shader}
		COMMENT "Compiling fragment shader ${shader} to ${shader_we}.dksh"
	)
	list(APPEND SHADER_DKSH "${shader_we}.dksh")
endforeach()

foreach(dksh ${SHADER_DKSH})
	get_filename_component(dksh_we ${dksh} NAME_WE)
	add_custom_command(OUTPUT "${dksh_we}_dksh.o"
		COMMAND ${CMAKE_OBJCOPY} --input binary --output elf32-littlearm
			                 --binary-architecture arm
			                 --rename-section .data=.rodata,alloc,load,readonly,data,contents
			                 "${dksh}"
			                 "${CMAKE_BINARY_DIR}/${dksh_we}_dksh.o"
		WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
		DEPENDS ${dksh}
		COMMENT "Objcopying ${dksh} to ${dksh_we}_dksh.o"
	)
	list(APPEND SHADER_OBJECTS "${dksh_we}_dksh.o")
endforeach()

add_executable(${CMAKE_PROJECT_NAME}.elf
	source/main.c
	source/load.c
	source/log.c
	source/main.c
	source/miniz.c
	source/module.c
	source/utils.c
	source/modules/SceCtrl.c
	source/modules/SceDisplay.c
	source/modules/SceGxm.c
	source/modules/SceKernelThreadMgr.c
	source/modules/SceLibKernel.c
	source/modules/SceSysmem.c
	source/modules/SceTouch.c
	${SHADER_OBJECTS}
)

target_link_libraries(${CMAKE_PROJECT_NAME}.elf
	${DEKO3DD_LIBRARY}
	${NX_LIBRARY}
	m
)

switch_create_nso(
	NAME ${CMAKE_PROJECT_NAME}
	ELF  ${CMAKE_PROJECT_NAME}.elf
)

switch_generate_npdm(
	NAME ${CMAKE_PROJECT_NAME}
	JSON ${CMAKE_SOURCE_DIR}/config.json
)

switch_create_nsp(
	NAME ${CMAKE_PROJECT_NAME}
	NSO  ${CMAKE_PROJECT_NAME}.nso
	NPDM ${CMAKE_PROJECT_NAME}.npdm
)
