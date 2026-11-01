#include "log.h"
#include "thread.h"

#include <stdarg.h>
#include <stdio.h>

static FILE *g_log;

void wlog_open(const char *path)
{
    if (path)
        g_log = fopen(path, "a");
}

void wlog_close(void)
{
    if (g_log)
        fclose(g_log);
    g_log = NULL;
}

void wlog(const char *fmt, ...)
{
    FILE *out = g_log ? g_log : stderr;
    unsigned long long ms = (unsigned long long)(wt_time_us() / 1000u);
    va_list ap;

    fprintf(out, "[%llu.%03llu] ", ms / 1000u, ms % 1000u);
    va_start(ap, fmt);
    vfprintf(out, fmt, ap);
    va_end(ap);
    fputc('\n', out);
    fflush(out);
}
