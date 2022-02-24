#pragma once
#include <stdint.h>

struct gl_shader_program;
struct tgsi_token;

typedef struct gl_shader_program* glsl_program;

enum pipeline_stage
{
	pipeline_stage_vertex,
	pipeline_stage_tess_ctrl,
	pipeline_stage_tess_eval,
	pipeline_stage_geometry,
	pipeline_stage_fragment,
	pipeline_stage_compute,
};

void glsl_frontend_init();
void glsl_frontend_exit();

glsl_program glsl_program_create(const char* source, pipeline_stage stage);
const tgsi_token* glsl_program_get_tokens(glsl_program prg, unsigned int& num_tokens);
void* glsl_program_get_constant_buffer(glsl_program prg, uint32_t& out_size);
int8_t const* glsl_program_vertex_get_in_locations(glsl_program prg);
unsigned glsl_program_compute_get_shared_size(glsl_program prg);
void glsl_program_free(glsl_program prg);
