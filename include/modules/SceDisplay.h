#ifndef SCE_DISPLAY_H
#define SCE_DISPLAY_H

#include <deko3d.h>

void SceDisplay_register(void);
int SceDisplay_init(DkDevice dk_device);
int SceDisplay_finish(void);

#endif
