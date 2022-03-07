#include <shader/spirv_recompiler.h>
#include <shader/usse_translator_types.h>
#include "vita3k_shader_recompiler_iface_c.h"

using namespace shader;

extern "C" {

bool convert_gxp_to_spirv_c(uint32_t **spirv, uint32_t *num_instr, const SceGxmProgram *program, const char *shader_name,
                            bool support_shader_interlock, bool support_texture_barrier, bool direct_fragcolor, bool spirv_shader,
                            const SceGxmVertexAttribute *hint_attributes, uint32_t num_hint_attributes,
                            bool maskupdate, bool force_shader_debug, bool (*dumper)(const char *ext, const char *dump))
{
    usse::SpirvCode code;
    FeatureState features;
    features.support_shader_interlock = support_shader_interlock;
    features.support_texture_barrier = support_texture_barrier;
    features.direct_fragcolor = direct_fragcolor;
    features.spirv_shader = spirv_shader;

    std::vector<SceGxmVertexAttribute> hint_attribs;
    for (uint32_t i = 0; i < num_hint_attributes; i++)
        hint_attribs.push_back(hint_attributes[i]);

    std::function<bool(const std::string &ext, const std::string &dump)> dumper_f = [dumper](const std::string &ext, const std::string &dump) {
        if (dumper)
            return dumper(ext.c_str(), dump.c_str());
        return true;
    };

    code = convert_gxp_to_spirv(*program, std::string(shader_name), features, &hint_attribs,
                                maskupdate, force_shader_debug, dumper_f);

    *num_instr = code.size();
    *spirv = (uint32_t *)malloc(sizeof(uint32_t) * code.size());
    memcpy(*spirv, code.data(), sizeof(uint32_t) * code.size());

    return true;
}

bool convert_gxp_to_glsl_c(char **glsl, const SceGxmProgram *program, const char *shader_name,
                           bool support_shader_interlock, bool support_texture_barrier, bool direct_fragcolor, bool spirv_shader,
                           const SceGxmVertexAttribute *hint_attributes, uint32_t num_hint_attributes,
                           bool maskupdate, bool force_shader_debug, bool (*dumper)(const char *ext, const char *dump))
{
    std::string code;
    FeatureState features;
    features.support_shader_interlock = support_shader_interlock;
    features.support_texture_barrier = support_texture_barrier;
    features.direct_fragcolor = direct_fragcolor;
    features.spirv_shader = spirv_shader;

    std::vector<SceGxmVertexAttribute> hint_attribs;
    for (uint32_t i = 0; i < num_hint_attributes; i++)
        hint_attribs.push_back(hint_attributes[i]);

    std::function<bool(const std::string &ext, const std::string &dump)> dumper_f = [dumper](const std::string &ext, const std::string &dump) {
        if (dumper)
            return dumper(ext.c_str(), dump.c_str());
        return true;
    };

    code = convert_gxp_to_glsl(*program, std::string(shader_name), features, &hint_attribs,
                                maskupdate, force_shader_debug, dumper_f);

    *glsl = (char *)malloc(code.size() + 1);
    strcpy(*glsl, code.c_str());

    return true;
}

}
