#include "logger.h"
#include <time.h>
#include <string.h>

static void timestamp_now(char buf[20]) {
    // 產生 YYYY-MM-DD HH:MM:SS
    time_t t = time(NULL);
    struct tm lt;

#if defined(_MSC_VER)
    // MSVC / Windows CRT：使用 localtime_s（thread-safe）
    localtime_s(&lt, &t);
#elif defined(__unix__) || defined(__APPLE__)
    // POSIX / Unix, macOS：使用 localtime_r（thread-safe）
    localtime_r(&t, &lt);
#else
    // 其他環境 / 部分 MinGW 後備：localtime（非 thread-safe，但可攜）
    struct tm *tmp = localtime(&t);
    if (tmp) lt = *tmp;
#endif

    trftime(buf, 20, "%Y-%m-%d %H:%M:%S", &lt);
}

static void vlog_emit(const char *level, const char *component,
                      const char *fmt, va_list ap, FILE *out) {
    char ts[20];
    timestamp_now(ts);
    fprintf(out, "%s [%s] %s: ", ts, level, component);
    vfprintf(out, fmt, ap);
    fputc('\n', out);
    fflush(out);
}

void log_info (const char *component, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vlog_emit("INFO", component, fmt, ap, stdout);
    va_end(ap);
}
void log_warn (const char *component, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vlog_emit("WARN", component, fmt, ap, stdout);
    va_end(ap);
}
void log_error(const char *component, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vlog_emit("ERROR", component, fmt, ap, stderr);
    va_end(ap);
}
