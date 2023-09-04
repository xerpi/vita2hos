#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "module.h"

const void *module_get_func_export(uint32_t lib_nid, uint32_t func_nid)
{
        size_t num_libs = NUM_EXPORTED_LIBS;

        for (size_t i = 0; i < num_libs; i++) {
                if (__start_exported_libraries[i].nid == lib_nid) {
                        const library_t *const lib = &__start_exported_libraries[i];
                        size_t num_funcs = NUM_FUNCS(lib);

                        for (size_t j = 0; j < num_funcs; j++) {
                                if (lib->func_nidtable_start[j] == func_nid)
                                        return lib->func_table_start[j];
                        }
                }
        }

        return NULL;
}