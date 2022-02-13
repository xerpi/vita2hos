#ifndef SCE_LIBKERNEL_H
#define SCE_LIBKERNEL_H

#include <switch.h>

#define SCE_ERROR_ERRNO_ENODEV  0x80010013
#define SCE_ERROR_ERRNO_EMFILE  0x80010018
#define SCE_ERROR_ERRNO_EBADF   0x80010009

int SceLibKernel_init(UEvent *process_exit_event_ptr, int *process_exit_res_ptr);

#endif
