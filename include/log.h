#ifndef LOG_H
#define LOG_H

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <switch.h>

#include "util.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LOG_DEVICE_SVC    = BIT(0),
    LOG_DEVICE_NETLOG = BIT(1),
    LOG_DEVICE_FILE   = BIT(2),
} log_device_t;

void log_init(uint32_t device_mask);
void log_print(const char *str);
void log_printf(const char *format, ...) __attribute__((format(printf, 1, 2)));

#define LOG(str, ...) log_printf(str, ##__VA_ARGS__)

#ifdef NDEBUG
#define LOG_DEBUG(str, ...) (void)0
#else
#define LOG_DEBUG(str, ...) log_printf(str, ##__VA_ARGS__)
#endif

void NORETURN fatal_error(const char *dialog_message, const char *fullscreen_message);

#ifdef __cplusplus
}
#endif

#endif
