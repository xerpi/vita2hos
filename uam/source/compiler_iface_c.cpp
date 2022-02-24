#include "compiler_iface.h"

extern "C" {

bool deko_compiler_compile_glsl(pipeline_stage stage, const char *glsl_source, void *buffer, uint32_t *size)
{
        DekoCompiler compiler{stage};
        bool rc = compiler.CompileGlsl(glsl_source);

        compiler.OutputDkshToMemory(buffer, size);

        return rc;
}

}