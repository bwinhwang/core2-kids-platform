#include "data_log.h"

#include <stdio.h>
#include <stdarg.h>

#include "esp_timer.h"

void data_log_begin(const char *name, const char *cols_csv)
{
    printf("#CSV-BEGIN %s\n", name ? name : "");
    printf("ts_ms,%s\n", cols_csv ? cols_csv : "");
}

void data_log_row(const char *fmt, ...)
{
    printf("%lld,", (long long)(esp_timer_get_time() / 1000));
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
}

void data_log_end(void)
{
    printf("#CSV-END\n");
}
