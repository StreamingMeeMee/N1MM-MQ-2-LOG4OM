#include "qsoid.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
static unsigned process_entropy(void) { return (unsigned)GetCurrentProcessId() * 2654435761u ^ (unsigned)GetTickCount(); }
#else
#include <unistd.h>
static unsigned process_entropy(void) { return (unsigned)getpid() * 2654435761u ^ (unsigned)clock(); }
#endif

void qsoid_generate(char *out) {
    static int seeded = 0;
    static time_t last_second = 0;
    static unsigned char used[1000];

    time_t now = time(NULL);
    if (!seeded) {
        /* Mix in the process id so two instances (or a restart) within the
         * same second don't produce the same sequence. */
        srand((unsigned)now ^ process_entropy() ^ (unsigned)(uintptr_t)&used);
        seeded = 1;
    }
    if (now != last_second) {
        memset(used, 0, sizeof(used));
        last_second = now;
    }

    int r = rand() % 1000;
    for (int i = 0; i < 1000 && used[r]; i++) r = (r + 1) % 1000;
    used[r] = 1;

    struct tm tmv;
#ifdef _WIN32
    localtime_s(&tmv, &now);
#else
    localtime_r(&now, &tmv);
#endif
    /* Unsigned and reduced so every field is provably its fixed width. */
    snprintf(out, QSOID_LEN + 1, "%04u%02u%02u%02u%02u%02u%03u",
             (unsigned)(tmv.tm_year + 1900) % 10000u, (unsigned)(tmv.tm_mon + 1) % 100u,
             (unsigned)tmv.tm_mday % 100u, (unsigned)tmv.tm_hour % 100u,
             (unsigned)tmv.tm_min % 100u, (unsigned)tmv.tm_sec % 100u, (unsigned)r % 1000u);
}
