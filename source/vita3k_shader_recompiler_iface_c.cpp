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
	GeneratedShader shader;
	FeatureState features;
	features.support_shader_interlock = support_shader_interlock;
	features.support_texture_barrier = support_texture_barrier;
	features.direct_fragcolor = direct_fragcolor;
	features.spirv_shader = spirv_shader;
	features.use_mask_bit = false;

	std::vector<SceGxmVertexAttribute> hint_attribs;
	for (uint32_t i = 0; i < num_hint_attributes; i++)
		hint_attribs.push_back(hint_attributes[i]);

	std::function<bool(const std::string &ext, const std::string &dump)> dumper_f = [dumper](const std::string &ext, const std::string &dump) {
		if (dumper)
			return dumper(ext.c_str(), dump.c_str());
		return true;
	};

	shader = convert_gxp(*program, std::string(shader_name), features, shader::Target::SpirVOpenGL,
			     &hint_attribs, maskupdate, force_shader_debug, dumper_f);

	*num_instr = shader.spirv.size();
	*spirv = (uint32_t *)malloc(sizeof(uint32_t) * shader.spirv.size());
	memcpy(*spirv, shader.spirv.data(), sizeof(uint32_t) * shader.spirv.size());

	return true;
}

bool convert_gxp_to_glsl_c(char **glsl, const SceGxmProgram *program, const char *shader_name,
			   bool support_shader_interlock, bool support_texture_barrier, bool direct_fragcolor, bool spirv_shader,
			   const SceGxmVertexAttribute *hint_attributes, uint32_t num_hint_attributes,
			   bool maskupdate, bool force_shader_debug, bool (*dumper)(const char *ext, const char *dump))
{
	GeneratedShader shader;
	FeatureState features;
	features.support_shader_interlock = support_shader_interlock;
	features.support_texture_barrier = support_texture_barrier;
	features.direct_fragcolor = direct_fragcolor;
	features.spirv_shader = spirv_shader;
	features.use_mask_bit = false;

	std::vector<SceGxmVertexAttribute> hint_attribs;
	for (uint32_t i = 0; i < num_hint_attributes; i++)
		hint_attribs.push_back(hint_attributes[i]);

	std::function<bool(const std::string &ext, const std::string &dump)> dumper_f = [dumper](const std::string &ext, const std::string &dump) {
		if (dumper)
			return dumper(ext.c_str(), dump.c_str());
		return true;
	};

	shader = convert_gxp(*program, std::string(shader_name), features, shader::Target::GLSLOpenGL,
			     &hint_attribs, maskupdate, force_shader_debug, dumper_f);

	*glsl = (char *)malloc(shader.glsl.size() + 1);
	strcpy(*glsl, shader.glsl.c_str());

	return true;
}

}
