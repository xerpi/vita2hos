#ifndef LOAD_H
#define LOAD_H

#include <switch.h>

int load_exe(Jit *jit, const char *filename, void **entry);

#endif
