#ifndef MACROS_H
#define MACROS_H

#include <limits.h>
#include <stdio.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FAILED(x) ((x))
#define SUCCEEDED(x) !((x))
#define MAX_PATH PATH_MAX 
#ifdef __linux__
#define BIT(x) (1<<(x)) 
#endif
#ifdef _DEBUG
#ifdef _3DS
#define CONSOLE_LOG_INLINE(fmt,...) \
{ \
    static char str_log_buffer[4096]; \
    sprintf(str_log_buffer, fmt, ##__VA_ARGS__); \
    printf("%s", str_log_buffer); \
}
#define CONSOLE_LOG(fmt,...) \
{ \
    static char str_log_buffer[4096]; \
    sprintf(str_log_buffer, fmt, ##__VA_ARGS__); \
    printf("%s\n", str_log_buffer); \
}
#elif __linux__
#define CONSOLE_LOG_INLINE(fmt,...) \
{ \
    char str_log_buffer[66000]; \
    sprintf(str_log_buffer, fmt, ##__VA_ARGS__); \
    printf("%s", str_log_buffer); \
}
#define CONSOLE_LOG(fmt,...) \
{ \
    char str_log_buffer[66000]; \
    sprintf(str_log_buffer, fmt, ##__VA_ARGS__); \
    printf("%s:%s:%d: %s\n", __FILE__, __FUNCTION__, __LINE__, str_log_buffer); \
}
#endif
#else
#define CONSOLE_LOG_INLINE(fmt,...)
#define CONSOLE_LOG(fmt,...)
#endif

    // fix some warnings
#ifdef NULL
#undef NULL
#endif
#define NULL 0

#ifdef __linux__ 
#define O_BINARY 0
#endif

#ifdef __cplusplus
}
#endif

#endif /* MACROS_H */

