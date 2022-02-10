#include <stdbool.h>
#include <stdlib.h>
#include <psp2/kernel/threadmgr.h>
#include "utils.h"
#include "log.h"

#define KERNEL_TLS_SIZE 0x800

static void *tls[1];

void *sceKernelGetTLSAddr(int key)
{
	return &tls[0];
}
