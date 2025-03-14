#include <stdarg.h>
#include <stdbool.h>
#include <switch.h>

#include "log.h"
#include "netlog.h"
#include "util.h"

static Mutex g_log_mutex;
static uint32_t g_device_mask;

const char log_file_path[] = "sdmc:/vita2hos/log.txt";

void file_log(const char *str)
{
    FILE *fp;

    fp = fopen(log_file_path, "a");
    if (!fp)
        return;

    fputs(str, fp);
    fputc('\n', fp);

    fclose(fp);
}

void log_init(uint32_t device_mask)
{
    mutexInit(&g_log_mutex);
    g_device_mask = device_mask;
}

void log_print(const char *str)
{
    const size_t len = strlen(str);

    mutexLock(&g_log_mutex);

    if (g_device_mask & LOG_DEVICE_SVC) {
        svcOutputDebugString(str, len);
        svcOutputDebugString("\n", 1);
    }

    if (g_device_mask & LOG_DEVICE_NETLOG) {
        netlog_write(str, len);
        netlog_write("\n", 1);
    }

    if (g_device_mask & LOG_DEVICE_FILE)
        file_log(str);

    mutexUnlock(&g_log_mutex);
}

void log_printf(const char *restrict format, ...)
{
    char buf[512];
    va_list argptr;

    va_start(argptr, format);
    vsnprintf(buf, sizeof(buf), format, argptr);
    va_end(argptr);

    log_print(buf);
}
