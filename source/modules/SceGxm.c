#include <psp2/gxm.h>
#include "SceGxm.h"
#include "module.h"
#include "utils.h"
#include "log.h"

typedef struct SceGxmContext {
        SceGxmContextParams params;
} SceGxmContext;

static SceGxmInitializeParams g_gxm_init_params;
static SceGxmContext g_gxm_context;

int sceGxmInitialize(const SceGxmInitializeParams *params)
{
        g_gxm_init_params = *params;
        return 0;
}

int sceGxmTerminate()
{
        return 0;
}

int sceGxmCreateContext(const SceGxmContextParams *params, SceGxmContext **context)
{
        g_gxm_context.params = *params;
        *context = &g_gxm_context;

        return 0;
}

int sceGxmDestroyContext(SceGxmContext *context)
{
        return 0;
}

void sceGxmFinish(SceGxmContext *context)
{

}

int sceGxmMapMemory(void *base, SceSize size, SceGxmMemoryAttribFlags attr)
{
        return 0;
}

int sceGxmUnmapMemory(void *base)
{
        return 0;
}

int sceGxmMapVertexUsseMemory(void *base, SceSize size, unsigned int *offset)
{
        return 0;
}

int sceGxmUnmapVertexUsseMemory(void *base)
{
        return 0;
}

int sceGxmMapFragmentUsseMemory(void *base, SceSize size, unsigned int *offset)
{
        return 0;
}

int sceGxmUnmapFragmentUsseMemory(void *base)
{
        return 0;
}

void SceGxm_register(void)
{
	static const export_entry_t exports[] = {
                {0xB0F1E4EC, sceGxmInitialize},
                {0xB627DE66, sceGxmTerminate},
                {0xE84CE5B4, sceGxmCreateContext},
                {0xEDDC5FB2, sceGxmDestroyContext},
                {0x0733D8AE, sceGxmFinish},
                {0xC61E34FC, sceGxmMapMemory},
                {0x828C68E8, sceGxmUnmapMemory},
                {0xFA437510, sceGxmMapVertexUsseMemory},
                {0x099134F5, sceGxmUnmapVertexUsseMemory},
                {0x008402C6, sceGxmMapFragmentUsseMemory},
                {0x80CCEDBB, sceGxmUnmapFragmentUsseMemory},
	};

	module_register_exports(exports, ARRAY_SIZE(exports));
}

int SceGxm_init(void)
{
	return 0;
}
