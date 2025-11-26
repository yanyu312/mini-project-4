#include "logger.h"
#include <stdio.h>

int main(void) {
    /* 使用預設 stdout/stderr */
    log_info("test", "This is an info message: %d", 1);
    log_warn("test", "This is a warning: %s", "be careful");
    log_error("test", "This is an error: code=%d", -1);

    /* 改變等級，只顯示 WARN 及以上 */
    log_set_level(LOG_LEVEL_WARN);
    log_info("test", "This info should NOT appear");
    log_warn("test", "This warn should appear");

    /* 將 info 輸出導向到檔案 */
    FILE *f = fopen("log_info.txt", "w");
    if (f) {
        log_set_info_fp(f);
        log_info("filetest", "Logged into file: %d", 123);
        fclose(f);
        /* 恢復為 stdout */
        log_set_info_fp(NULL);
    }

    return 0;
}
