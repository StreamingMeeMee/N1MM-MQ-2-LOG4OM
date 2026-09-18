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

#define LOG_PAYLOAD_MAX 8192

void log_payload(const char *body, size_t len) {
    if (!g_verbose) return;

    size_t shown = len < LOG_PAYLOAD_MAX ? len : LOG_PAYLOAD_MAX;
    printf("  payload (%zu bytes): ", len);
    for (size_t i = 0; i < shown; i++) {
        unsigned char c = (unsigned char)body[i];
        if (c == '\n') fputs("\\n", stdout);
        else if (c == '\r') fputs("\\r", stdout);
        else if (c == '\t') fputs("\\t", stdout);
        else if (c == '\\') fputs("\\\\", stdout);
        else if (c >= 0x20 && c <= 0x7E) fputc(c, stdout);
        else printf("\\x%02X", c);
    }
    if (shown < len) printf("... [%zu more bytes not shown]", len - shown);
    fputc('\n', stdout);
    fflush(stdout);
}
