#include "log.h"

#include <stdio.h>
#include <time.h>

static int g_verbose = 0;

void log_init(int verbose) {
    g_verbose = verbose;
}

void log_message(const char *msg_type, const char *status) {
    if (!g_verbose) return;

    time_t now = time(NULL);
    struct tm tmv;
#ifdef _WIN32
    localtime_s(&tmv, &now);
#else
    localtime_r(&now, &tmv);
#endif
    char timebuf[32];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &tmv);

    printf("[%s] type=%s status=%s\n", timebuf, msg_type, status);
    fflush(stdout);
}
