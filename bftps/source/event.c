#include <stdlib.h>
#include <errno.h>

#ifdef __linux__
#define _GNU_SOURCE     1       /* See feature_test_macros(7) */
#define __USE_GNU	1       /* For some reason defining only _GNU_SOURCE doesn't seem to work */
#include <fcntl.h>              /* Obtain O_* constant definitions */
#include <unistd.h>
#include <poll.h>
#include <string.h>
#elif _3DS
#include <3ds.h>
#endif

#include "event.h"

#ifdef __linux__
typedef struct
{
   int pipeFd[2];
   char buffer[256]; // 256 is arbitrary 
} event_handle_linux;
#endif

int event_create(event_handle_t* event)
{
    if(!event || *event) return EINVAL;
    
    int nErrorCode = ENOSYS;
#ifdef __linux__
    event_handle_linux* eventLinux = malloc(sizeof(event_handle_linux));
    if(!eventLinux)
        nErrorCode = errno;
    else
    {
        memset(eventLinux, 0, sizeof(event_handle_linux));
        if(0 > pipe2(eventLinux->pipeFd, O_NONBLOCK))
        {
            nErrorCode = errno;
            free(eventLinux);
        }
        else
        {
            *event = eventLinux;
            nErrorCode = 0;
        }
    }
#elif _3DS
    Handle* event3DS = malloc(sizeof(Handle));
    if (!event3DS)
        nErrorCode = errno;
    else {
        if (R_SUCCEEDED(nErrorCode = svcCreateEvent(event3DS, RESET_STICKY))) {
            *event = event3DS;
            nErrorCode = 0;
        }
    }
#endif
    
    return nErrorCode;
}

int event_set(event_handle_t event)
{
    if(!event) return EINVAL;
    
    int nErrorCode = ENOSYS;
#ifdef __linux__
    event_handle_linux* eventLinux = (event_handle_linux*)event;
    if(0 > write(eventLinux->pipeFd[1], &eventLinux->buffer, 1))
        nErrorCode = errno;
    else
        nErrorCode = 0;
#elif _3DS
    Handle* event3DS = (Handle*) event;
    if(R_SUCCEEDED(nErrorCode = svcSignalEvent(*event3DS)))
        nErrorCode = 0;
#endif
    
    return nErrorCode;
}

int event_reset(event_handle_t event)
{
    if(!event) return EINVAL;
    
    int nErrorCode = ENOSYS;
#ifdef __linux__
    event_handle_linux* eventLinux = (event_handle_linux*)event;
    // Try read the first time just to check for errors    
    if(0 > read(eventLinux->pipeFd[0], &eventLinux->buffer, sizeof(eventLinux->buffer)))
        nErrorCode = errno;
    else
    {
        // finish cleaning the buffer        
        while( read(eventLinux->pipeFd[0], &eventLinux->buffer, sizeof(eventLinux->buffer)) > 0 );
        nErrorCode = 0;
    }    
#elif _3DS
    Handle* event3DS = (Handle*) event;
    if(R_SUCCEEDED(nErrorCode = svcClearEvent(*event3DS)))
        nErrorCode = 0;
#endif
    
    return nErrorCode;
}

int event_wait(event_handle_t event, int timeout_ms)
{
    if(!event) return EINVAL;
    
    int nErrorCode = ENOSYS;
#ifdef __linux__
    event_handle_linux* eventLinux = (event_handle_linux*)event;
    
    struct pollfd fds[1];
    fds[0].fd = eventLinux->pipeFd[0];
    fds[0].events = POLLRDNORM;
    int result = poll(fds, 1, timeout_ms);
    if(0 > result) 
        nErrorCode = errno;
    else if(0 == result)
        nErrorCode = ETIME;
    else 
        nErrorCode = 0;
#elif _3DS
    Handle* event3DS = (Handle*) event;
    s64 timeout_ns = (s64)timeout_ms * (s64)1000 /*us*/ * (s64)1000 /*ns*/;
    nErrorCode = svcWaitSynchronization(*event3DS, timeout_ns);
#endif
    
    return nErrorCode;
}

int event_destroy(event_handle_t* event)
{
     if(!event || !(*event)) return EINVAL;
    
    int nErrorCode = ENOSYS;
#ifdef __linux__
    event_handle_linux* eventLinux = (event_handle_linux*)*event;
    int result1 = 0;
    if(0 > close(eventLinux->pipeFd[0]))
        result1 = errno;
    int result2 = 0;
    if(0 > close(eventLinux->pipeFd[1]))
        result2 = errno;
    free(eventLinux);
    
    if(result1)
        nErrorCode = result1;
    else
        nErrorCode = result2;   
#elif _3DS
    Handle* event3DS = (Handle*) *event;
    if(R_SUCCEEDED(nErrorCode = svcCloseHandle(*event3DS)))
        nErrorCode = 0;
    free(event3DS);
#endif
    
    *event = NULL;
    
    return nErrorCode;
}