#ifndef VITA3K_SHADER_RECOMPILER_IFACE_C_H
#define VITA3K_SHADER_RECOMPILER_IFACE_C_H

#ifdef __cplusplus
extern "C" {
#endif

bool convert_gxp_to_spirv_c(uint32_t **spirv, uint32_t *num_instr, const SceGxmProgram *program, const char *shader_name,
                            bool support_shader_interlock, bool support_texture_barrier, bool direct_fragcolor, bool spirv_shader,
                            const SceGxmVertexAttribute *hint_attributes, uint32_t num_hint_attributes,
                            bool maskupdate, bool force_shader_debug, bool (*dumper)(const char *ext, const char *dump));

bool convert_gxp_to_glsl_c(char **glsl, const SceGxmProgram *program, const char *shader_name,
                           bool support_shader_interlock, bool support_texture_barrier, bool direct_fragcolor, bool spirv_shader,
                           const SceGxmVertexAttribute *hint_attributes, uint32_t num_hint_attributes,
                           bool maskupdate, bool force_shader_debug, bool (*dumper)(const char *ext, const char *dump));

#ifdef __cplusplus
}
#endif

#endif
