#ifndef MODULE_H
#define MODULE_H

#include <stdint.h>

typedef struct {
    const char *name;
    uint32_t nid;
    const uint32_t *func_nidtable_start;
    const uint32_t *func_nidtable_stop;
    const void **func_table_start;
    const void **func_table_stop;
} library_t;

extern const library_t __start_exported_libraries[];
extern const library_t __stop_exported_libraries[];

#define DECLARE_LIBRARY(_name, _nid) \
    extern const uint32_t __start_ ## _name ## _func_nidtable[]; \
    extern const uint32_t __stop_ ## _name ## _func_nidtable[]; \
    extern const void *__start_ ## _name ## _func_table[]; \
    extern const void *__stop_ ## _name ## _func_table[]; \
    static const library_t _name ## _library __attribute__((used, section("exported_libraries"))) = { \
        .name = #_name, \
        .nid = _nid, \
        .func_nidtable_start = __start_ ## _name ## _func_nidtable, \
        .func_nidtable_stop = __stop_ ## _name ## _func_nidtable, \
        .func_table_start = __start_ ## _name ## _func_table, \
        .func_table_stop = __stop_ ## _name ## _func_table \
    }

#define EXPORT(library, nid, type, name, ...) \
    type name(__VA_ARGS__); \
    static const uint32_t library ## _ ## name ## _ ## nid ## _nid __attribute__((used, section(#library "_func_nidtable"))) = nid; \
    static const void *const library ## _ ## name ## _ ## nid ## _func __attribute__((used, section(#library "_func_table"))) = &name; \
    type name(__VA_ARGS__)

#define NUM_EXPORTED_LIBS (((uintptr_t)&__stop_exported_libraries - (uintptr_t)&__start_exported_libraries) / sizeof(library_t))
#define NUM_FUNCS(lib) (((uintptr_t)lib->func_table_stop - (uintptr_t)lib->func_table_start) / sizeof(void *))

const void *module_get_func_export(uint32_t lib_nid, uint32_t func_nid);

#endif