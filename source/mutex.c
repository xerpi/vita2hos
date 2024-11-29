#include "compiler_compat.h"
#include "mutex.h"
#include <switch.h>

void vita2hos_rmutex_init(vita2hos_rmutex_t* m) {
    rmutexInit(&m->mutex);
}

void vita2hos_rmutex_lock(vita2hos_rmutex_t* m) {
    rmutexLock(&m->mutex);
}

bool vita2hos_rmutex_trylock(vita2hos_rmutex_t* m) {
    return rmutexTryLock(&m->mutex);
}

void vita2hos_rmutex_unlock(vita2hos_rmutex_t* m) {
    rmutexUnlock(&m->mutex);
}

void rmutexInit(RMutex* m) {
    if (!m) return;
    m->counter = 0;
    m->owner = 0;
}

void rmutexLock(RMutex* m) {
    if (!m) return;
    
    uint32_t current_thread = (uint32_t)threadGetCurrentHandle();
    
    // If already owned by current thread, just increment counter
    if (m->owner == current_thread) {
        m->counter++;
        return;
    }
    
    // Wait until we can acquire the lock
    while (1) {
        uint32_t expected = 0;
        if (__sync_bool_compare_and_swap(&m->counter, expected, 1)) {
            m->owner = current_thread;
            break;
        }
        svcSleepThread(1000000); // 1ms
    }
}

bool rmutexTryLock(RMutex* m) {
    if (!m) return false;
    
    uint32_t current_thread = (uint32_t)threadGetCurrentHandle();
    
    // If already owned by current thread, just increment counter
    if (m->owner == current_thread) {
        m->counter++;
        return true;
    }
    
    // Try to acquire the lock
    uint32_t expected = 0;
    if (__sync_bool_compare_and_swap(&m->counter, expected, 1)) {
        m->owner = current_thread;
        return true;
    }
    
    return false;
}

void rmutexUnlock(RMutex* m) {
    if (!m) return;
    
    uint32_t current_thread = (uint32_t)threadGetCurrentHandle();
    
    // Only the owner can unlock
    if (m->owner != current_thread) {
        return;
    }
    
    // Decrement counter
    m->counter--;
    
    // If this was the last lock, release ownership
    if (m->counter == 0) {
        m->owner = 0;
        __sync_bool_compare_and_swap(&m->counter, 1, 0);
    }
}
