#ifndef SCE_KERNEL_THREAD_MGR_H
#define SCE_KERNEL_THREAD_MGR_H

#include <psp2/kernel/threadmgr.h>

typedef struct {
    uint32_t index;
    Thread thread;
    SceUID uid;
    char name[32];
    SceKernelThreadEntry entry;
    SceSize arglen;
    void *argp;
    int return_status;
    void **vita_tls;
} VitaThreadInfo;

int SceKernelThreadMgr_init(void);
int SceKernelThreadMgr_finish(void);
VitaThreadInfo *SceKernelThreadMgr_get_thread_info(void);
int SceKernelThreadMgr_main_entry(SceKernelThreadEntry entry, int args, void *argp);

#endif
