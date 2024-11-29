#pragma once

// Prevent libnx mutex.h from being included
#define __SWITCH_MUTEX_H__

#include <stddef.h>
#include <stdint.h>

// Our own mutex implementation based on libnx's structure
typedef struct {
    union {
        uint32_t counter;
        struct {
            uint16_t cur_count;
            uint16_t max_count;
        };
    };
    uint32_t owner;
} vita2hos_mutex_t;

typedef vita2hos_mutex_t RMutex;

// Define types that might conflict
#ifndef __int64_t_defined
typedef int64_t __int64_t;
typedef uint64_t __uint64_t;
typedef __int64_t __int_least64_t;
typedef __uint64_t __uint_least64_t;
typedef long long __intmax_t;
typedef unsigned long long __uintmax_t;
#endif

#ifndef __intptr_t_defined
typedef int __intptr_t;
typedef unsigned int __uintptr_t;
#define __intptr_t_defined
#endif

typedef unsigned int __fsblkcnt_t;
typedef unsigned int __fsfilcnt_t;
typedef unsigned int __dev_t;
typedef unsigned int __uid_t;
typedef unsigned int __gid_t;
typedef unsigned int __ino_t;
typedef int __key_t;
typedef int __ssize_t;
typedef long __clock_t;
typedef int __clockid_t;
typedef int __daddr_t;
typedef void* __timer_t;
typedef unsigned int __nlink_t;
typedef unsigned int __useconds_t;

// Define structures for stdio
struct __FILE;
typedef struct __FILE FILE;

// Define structures for stdlib
typedef struct {
    int quot;
    int rem;
} div_t;

typedef struct {
    long quot;
    long rem;
} ldiv_t;

typedef struct {
    long long quot;
    long long rem;
} lldiv_t;

// Define functions with correct exception specifiers
#ifdef __cplusplus
extern "C" {
#endif

void abort(void) __attribute__((__noreturn__));
int abs(int) __attribute__((__const__));
int atexit(void (*)(void)) __attribute__((__nonnull__(1)));
double atof(const char*) __attribute__((__pure__));
int atoi(const char*) __attribute__((__pure__));
long atol(const char*) __attribute__((__pure__));
void *calloc(size_t, size_t) __attribute__((__malloc__));
div_t div(int, int) __attribute__((__const__));
void exit(int) __attribute__((__noreturn__));
void free(void*);
char *getenv(const char*) __attribute__((__nonnull__(1)));
long labs(long) __attribute__((__const__));
ldiv_t ldiv(long, long) __attribute__((__const__));
void *malloc(size_t) __attribute__((__malloc__));
int mblen(const char*, size_t);
int mbtowc(wchar_t*, const char*, size_t);
int wctomb(char*, wchar_t);
size_t mbstowcs(wchar_t*, const char*, size_t);
size_t wcstombs(char*, const wchar_t*, size_t);
int rand(void);
void *realloc(void*, size_t);
void srand(unsigned);
double strtod(const char*, char**);
float strtof(const char*, char**);
long strtol(const char*, char**, int);
unsigned long strtoul(const char*, char**, int);

#ifdef __cplusplus
}
#endif
