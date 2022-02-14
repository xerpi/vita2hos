#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "module.h"

static export_entry_t *g_exports = NULL;
static size_t g_exports_num = 0;

void module_finish(void)
{
        free(g_exports);
        g_exports_num = 0;
}

void module_register_exports(const export_entry_t *entries, uint32_t count)
{
        g_exports = realloc(g_exports, (g_exports_num + count) * sizeof(export_entry_t));
        memcpy(&g_exports[g_exports_num], entries, count * sizeof(export_entry_t));
        g_exports_num += count;
}

const void *module_get_export_addr(uint32_t nid)
{
        for (size_t i = 0; i < g_exports_num; i++) {
                if (g_exports[i].nid == nid)
                        return g_exports[i].addr;
        }

        return NULL;
}