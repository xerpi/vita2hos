#ifndef PROTECTED_BITSET_H
#define PROTECTED_BITSET_H

#include <switch.h>
#include "bitset.h"

#define DECL_PROTECTED_BITSET(type, prefix, size)       \
        static type g_##prefix[size];                   \
        static BITSET_DEFINE(g_##prefix##_valid, size); \
        static Mutex g_##prefix##_mutex;

#define DECL_PROTECTED_BITSET_ALLOC(name, prefix, type)              \
static type *name(void)                                              \
{                                                                    \
        uint32_t index;                                              \
                                                                     \
	mutexLock(&g_##prefix##_mutex);                              \
	index = bitset_find_first_clear_and_set(g_##prefix##_valid); \
	mutexUnlock(&g_##prefix##_mutex);                            \
                                                                     \
	if (index == UINT32_MAX)                                     \
		return NULL;                                         \
                                                                     \
	g_##prefix[index].index = index;                             \
                                                                     \
	return &g_##prefix[index];                                   \
}

#define DECL_PROTECTED_BITSET_RELEASE(name, prefix, type) \
static void name(type *var)                               \
{                                                         \
	mutexLock(&g_##prefix##_mutex);                   \
	BITSET_CLEAR(g_##prefix##_valid, var->index);     \
	mutexUnlock(&g_##prefix##_mutex);                 \
}

#define DECL_PROTECTED_BITSET_GET_FOR_UID(name, prefix, type) \
static type *name(SceUID uid)                       \
{                                                             \
	mutexLock(&g_##prefix##_mutex);                       \
	bitset_for_each_bit_set(g_##prefix##_valid, index) {  \
		if (g_##prefix[index].uid == uid) {           \
			mutexUnlock(&g_##prefix##_mutex);     \
			return &g_##prefix[index];            \
		}                                             \
	}                                                     \
	mutexUnlock(&g_##prefix##_mutex);                     \
	return NULL;                                          \
}

#endif