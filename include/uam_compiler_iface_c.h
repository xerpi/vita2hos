#ifndef UAM_COMPILER_IFACE_C_H
#define UAM_COMPILER_IFACE_C_H

typedef struct gl_shader_program gl_shader_program;
typedef struct tgsi_token tgsi_token;
typedef enum pipeline_stage pipeline_stage;

#include "uam/glsl_frontend.h"

#ifdef __cplusplus
extern "C" {
#endif

bool uam_compiler_compile_glsl(pipeline_stage stage, const char *glsl_source, void *buffer, uint32_t *size);

#ifdef __cplusplus
}
#endif

#endif
