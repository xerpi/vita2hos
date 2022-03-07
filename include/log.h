#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <stdbool.h>
#include "utils.h"

#define DEBUG
#define VERBOSE_LOGGING

#ifdef DEBUG
    #define DEBUG_LOGGING       1                     ///< Enable debug logging
#else
    #define DEBUG_LOGGING       0                     ///< Disable debug logging
#endif

#ifndef VERBOSE_LOGGING
    #define VERBOSE_LOGGING     0                     ///< Disable verbose logging
#else
    #undef VERBOSE_LOGGING
    #ifndef DEBUG_LOGGING
        #define DEBUG_LOGGING   1                     ///< Enabled automatically when verbose logging
    #endif
    #define VERBOSE_LOGGING     1                     ///< Enable verbose logging
#endif

#define MAX_LOG_LENGTH  0x100                 ///< Any log entry larger than this will cause a buffer overflow!
#define IF_DEBUG        if (DEBUG_LOGGING)    ///< Place before calling @c LOG to only show when debugging
#define IF_VERBOSE      if (VERBOSE_LOGGING)  ///< Place before calling @c LOG to only show when verbose output is enabled
#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
//#define LOG(...)        {printf("%s:%d: ", __FILENAME__, __LINE__); printf(__VA_ARGS__); puts("");}  ///< Write a log entry

extern bool log_to_fb_console;

void LOGSTR(const char *str);
void __attribute__((format(printf, 1, 2))) LOG(const char *fmt, ...);
void NORETURN fatal_error(const char *dialog_message, const char *fullscreen_message);

#endif
