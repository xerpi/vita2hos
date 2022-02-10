#ifndef LOAD_H
#define LOAD_H

#include <switch.h>

int load_exe(Jit *jit, const char *filename, void **entry);
int load_elf(Jit *jit, const void *data, void **entry);

#endif
