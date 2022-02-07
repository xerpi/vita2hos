#ifndef UTILS_H
#define UTILS_H

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#define MIN2(x, y)	(((x) < (y)) ? (x) : (y))
#define MAX2(x, y)	(((x) > (y)) ? (x) : (y))
#define ALIGN(x, a)     (((x) + ((a) - 1)) & ~((a) - 1))
#define ROUNDUP32(x)	(((u32)(x) + 0x1f) & ~0x1f)
#define ROUNDDOWN32(x)	(((u32)(x) - 0x1f) & ~0x1f)

#define UNUSED(x) (void)(x)
#define MEMBER_SIZE(type, member) sizeof(((type *)0)->member)

#define STRINGIFY(x)	#x
#define TOSTRING(x)	STRINGIFY(x)

int utils_load_file(const char *filename, void **data, uint32_t *size);

#endif
