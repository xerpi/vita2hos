#ifndef COMPILER_TYPES_H
#define COMPILER_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef signed short int16_t;
typedef unsigned short uint16_t;
typedef signed int int32_t;
typedef unsigned int uint32_t;
typedef signed long long int64_t;
typedef unsigned long long uint64_t;

typedef uint64_t uintptr_t;
typedef int64_t intptr_t;
typedef uint64_t size_t;
typedef int64_t ssize_t;

#ifdef __cplusplus
}
#endif

#endif // COMPILER_TYPES_H
