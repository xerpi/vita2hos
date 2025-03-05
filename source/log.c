#include <stdarg.h>
#include <stdbool.h>
#include <switch.h>

#include "util.h"

bool log_to_fb_console;

#ifdef VITA2HOS_LOG_TO_FILE
const char log_file_path[] = "sdmc:/vita2hos/debug.log";

void flog(const char *str)
{
    FILE *fp;

    fp = fopen(log_file_path, "a");
    if (!fp)
        return;

    fputs(str, fp);
    fputc('\n', fp);

    fclose(fp);
}
#endif

void LOGSTR(const char *str)
{
    svcOutputDebugString(str, strlen(str));

    if (log_to_fb_console)
        puts(str);

#ifdef VITA2HOS_LOG_TO_FILE
    flog(str);
#endif
}

void __attribute__((format(printf, 1, 2))) LOG(const char *fmt, ...)
{
    char buf[256];
    va_list argptr;
    int n;

    va_start(argptr, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, argptr);
    va_end(argptr);

    svcOutputDebugString(buf, n);

    if (log_to_fb_console)
        puts(buf);

#ifdef VITA2HOS_LOG_TO_FILE
    flog(buf);
#endif
}
