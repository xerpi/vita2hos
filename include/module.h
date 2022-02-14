#ifndef MODULE_H
#define MODULE_H

#include <stdint.h>

typedef struct {
	uint32_t nid;
	void *addr;
} export_entry_t;

void module_finish(void);
void module_register_exports(const export_entry_t *entries, uint32_t count);
const void *module_get_export_addr(uint32_t nid);

#endif