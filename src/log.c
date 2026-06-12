#include "appimagetool.h"
#include <stdatomic.h>
#include <stdarg.h>

static atomic_int g_level = LOG_LEVEL_INFO;

void log_init(int verbosity)
{
    int level = verbosity;
    if (level < LOG_LEVEL_ERROR) level = LOG_LEVEL_ERROR;
    if (level > LOG_LEVEL_DEBUG) level = LOG_LEVEL_DEBUG;
    atomic_store(&g_level, level);
}

void log_debug(const char *msg)
{
    if (atomic_load(&g_level) >= LOG_LEVEL_DEBUG)
        fprintf(stderr, "[DEBUG] %s\n", msg);
}

void log_info(const char *msg)
{
    if (atomic_load(&g_level) >= LOG_LEVEL_INFO)
        fprintf(stderr, "%s\n", msg);
}

void log_warn(const char *msg)
{
    if (atomic_load(&g_level) >= LOG_LEVEL_WARN)
        fprintf(stderr, "WARNING: %s\n", msg);
}

void log_error(const char *msg)
{
    fprintf(stderr, "error: %s\n", msg);
}

void log_debug2(const char *file, int line, const char *fmt, ...)
{
    if (atomic_load(&g_level) < LOG_LEVEL_DEBUG) return;
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fprintf(stderr, "[DEBUG] %s:%d %s\n", file, line, buf);
}

void log_info2(const char *fmt, ...)
{
    if (atomic_load(&g_level) < LOG_LEVEL_INFO) return;
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fprintf(stderr, "%s\n", buf);
}

void log_warn2(const char *fmt, ...)
{
    if (atomic_load(&g_level) < LOG_LEVEL_WARN) return;
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fprintf(stderr, "WARNING: %s\n", buf);
}

void log_error2(const char *fmt, ...)
{
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fprintf(stderr, "error: %s\n", buf);
}
