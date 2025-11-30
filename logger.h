#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>
#include <stdarg.h>

void log_info (const char *component, const char *fmt, ...);
void log_warn (const char *component, const char *fmt, ...);
void log_error(const char *component, const char *fmt, ...);

#endif
