#include <uam/compiler_iface.h>
#include "uam_compiler_iface_c.h"
#include <fstream>

extern "C" {

bool uam_compiler_compile_glsl(pipeline_stage stage, const char *glsl_source, void *buffer, uint32_t *size)
{
        DekoCompiler compiler{stage};
        bool rc = compiler.CompileGlsl(glsl_source);
        if (!rc)
                return false;

        // Create a temporary file
        const char* tempFile = "/tmp/shader.dksh";
        compiler.OutputDksh(tempFile);

        // Read the file back into memory
        std::ifstream file(tempFile, std::ios::binary | std::ios::ate);
        if (!file.is_open())
                return false;

        std::streamsize fileSize = file.tellg();
        file.seekg(0, std::ios::beg);

        if (fileSize > *size)
                return false;

        file.read((char*)buffer, fileSize);
        *size = fileSize;

        // Clean up
        file.close();
        remove(tempFile);

        return rc;
}

}
