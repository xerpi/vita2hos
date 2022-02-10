#ifndef SCE_KERNEL_THREAD_MGR_H
#define SCE_KERNEL_THREAD_MGR_H

#include <psp2common/types.h>

typedef struct {
	bool valid;
	SceUID thid;
	void **vita_tls;
} VitaThreadInfo;

int SceKernelThreadMgr_init(void);
VitaThreadInfo *SceKernelThreadMgr_get_thread_info(void);

#endif
