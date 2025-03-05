#include <cstring>
#include <shader/spirv_recompiler.h>
#include <shader/usse_translator_types.h>

#include "vita3k_shader_recompiler_iface_c.h"

extern "C" {

static shader::GeneratedShader
convert_gxp_internal(const SceGxmProgram *program, const char *shader_name,
                     bool support_shader_interlock, bool support_texture_barrier,
                     bool direct_fragcolor, bool spirv_shader,
                     const SceGxmVertexAttribute *hint_attributes, uint32_t num_hint_attributes,
                     bool maskupdate, bool force_shader_debug,
                     bool (*dumper)(const char *ext, const char *dump), shader::Target target)
{
    shader::GeneratedShader shader;
    shader::Hints hints;
    FeatureState features;
    features.support_shader_interlock = support_shader_interlock;
    features.support_texture_barrier = support_texture_barrier;
    features.direct_fragcolor = direct_fragcolor;
    features.spirv_shader = spirv_shader;
    features.use_mask_bit = false;

    std::vector<SceGxmVertexAttribute> hint_attribs;
    for (uint32_t i = 0; i < num_hint_attributes; i++)
        hint_attribs.push_back(hint_attributes[i]);

    hints.attributes = &hint_attribs;
    /* TODO: Fill this properly */
    hints.color_format = SCE_GXM_COLOR_FORMAT_U8U8U8U8_ABGR;
    std::fill_n(hints.vertex_textures, SCE_GXM_MAX_TEXTURE_UNITS,
                SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR);
    std::fill_n(hints.fragment_textures, SCE_GXM_MAX_TEXTURE_UNITS,
                SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR);

    std::function<bool(const std::string &ext, const std::string &dump)> dumper_f =
        [dumper](const std::string &ext, const std::string &dump) {
            if (dumper)
                return dumper(ext.c_str(), dump.c_str());
            return true;
        };

    return shader::convert_gxp(*program, std::string(shader_name), features, target, hints,
                               maskupdate, force_shader_debug, dumper_f);
}

bool convert_gxp_to_spirv_c(uint32_t **spirv, uint32_t *num_instr, const SceGxmProgram *program,
                            const char *shader_name, bool support_shader_interlock,
                            bool support_texture_barrier, bool direct_fragcolor, bool spirv_shader,
                            const SceGxmVertexAttribute *hint_attributes,
                            uint32_t num_hint_attributes, bool maskupdate, bool force_shader_debug,
                            bool (*dumper)(const char *ext, const char *dump))
{
    shader::GeneratedShader shader;

    shader = convert_gxp_internal(program, shader_name, support_shader_interlock,
                                  support_texture_barrier, direct_fragcolor, spirv_shader,
                                  hint_attributes, num_hint_attributes, maskupdate,
                                  force_shader_debug, dumper, shader::Target::SpirVOpenGL);

    *num_instr = shader.spirv.size();
    *spirv = (uint32_t *)malloc(sizeof(uint32_t) * shader.spirv.size());
    memcpy(*spirv, shader.spirv.data(), sizeof(uint32_t) * shader.spirv.size());

    return true;
}

bool convert_gxp_to_glsl_c(char **glsl, const SceGxmProgram *program, const char *shader_name,
                           bool support_shader_interlock, bool support_texture_barrier,
                           bool direct_fragcolor, bool spirv_shader,
                           const SceGxmVertexAttribute *hint_attributes,
                           uint32_t num_hint_attributes, bool maskupdate, bool force_shader_debug,
                           bool (*dumper)(const char *ext, const char *dump))
{
    shader::GeneratedShader shader;

    shader = convert_gxp_internal(program, shader_name, support_shader_interlock,
                                  support_texture_barrier, direct_fragcolor, spirv_shader,
                                  hint_attributes, num_hint_attributes, maskupdate,
                                  force_shader_debug, dumper, shader::Target::GLSLOpenGL);

    *glsl = (char *)malloc(shader.glsl.size() + 1);
    strcpy(*glsl, shader.glsl.c_str());

    return true;
}
}
