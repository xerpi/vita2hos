#ifndef SCE_LIBKERNEL_H
#define SCE_LIBKERNEL_H

#include <switch.h>

int SceLibKernel_init(UEvent *process_exit_event_ptr, int *process_exit_res_ptr);

#endif
