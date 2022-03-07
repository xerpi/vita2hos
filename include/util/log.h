#ifndef UTIL_LOG_H
#define UTIL_LOG_H

#include "../log.h"

static inline void LOGFMT(const char *str, ...)
{
	LOGSTR(str);
}

#define LOG_TRACE LOGFMT
#define LOG_DEBUG LOGFMT
#define LOG_INFO LOGFMT
#define LOG_WARN LOGFMT
#define LOG_ERROR LOGFMT
#define LOG_CRITICAL LOGFMT

#endif
