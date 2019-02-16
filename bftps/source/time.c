#ifdef __linux__
#include <unistd.h>
#elif _3DS
#include <3ds.h>
#endif

#include "time.h"

void time_sleep(int time_ms) {
#ifdef __linux__
    usleep(time_ms * 1000);
#elif _3DS
    s64 time_ns = (s64) time_ms * (s64) 1000 /*us*/ * (s64) 1000 /*ns*/;
    svcSleepThread(time_ns);
#endif
}