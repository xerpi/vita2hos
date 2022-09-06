#ifndef MODULE_H
#define MODULE_H

#include <stdint.h>

#define GET_EXPORT_NID(name) __nid_ ## name
#define EXPORT(type, name, nid, ...) static const uint32_t GET_EXPORT_NID(name) = nid; type name(__VA_ARGS__)
#define EXPORT_STUB EXPORT
#define GET_EXPORT_ENTRY(name) {GET_EXPORT_NID(name), name}

typedef struct {
	uint32_t nid;
	void *addr;
} export_entry_t;

void module_finish(void);
void module_register_exports(const export_entry_t *entries, uint32_t count);
const void *module_get_export_addr(uint32_t nid);

#endif