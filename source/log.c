#include <stdarg.h>
#include <stdbool.h>
#include <switch.h>
#include "utils.h"

bool log_to_fb_console;

void LOGSTR(const char *str)
{
	svcOutputDebugString(str, strlen(str));

	if (log_to_fb_console)
		puts(str);
}

void __attribute__((format(printf, 1, 2))) LOG(const char *fmt, ...)
{
	static char buf[8*1024];
	va_list argptr;
	int n;

	va_start(argptr, fmt);
	n = vsnprintf(buf, sizeof(buf), fmt, argptr);
	va_end(argptr);

	svcOutputDebugString(buf, n);

	if (log_to_fb_console)
		puts(buf);
}
