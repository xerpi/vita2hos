#pragma once

#include "compiler_compat.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Basic mutex functions using libnx's implementation
void rmutexInit(RMutex* m);
void rmutexLock(RMutex* m);
bool rmutexTryLock(RMutex* m);
void rmutexUnlock(RMutex* m);

#ifdef __cplusplus
}
#endif
