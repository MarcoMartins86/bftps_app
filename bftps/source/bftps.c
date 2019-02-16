#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#ifdef _3DS
#include <3ds.h>
#endif

#include "thread.h"
#include "bftps.h"
#include "event.h"
#include "time.h"
#include "bftps_session.h"
#include "bftps_socket.h"

#include "macros.h"

#ifdef _3DS
// values taken from socket example
#define SOCU_ALIGN      0x1000
#define SOCU_BUFFERSIZE 0x100000
// needed to be static because we only want to allocate memory once
static u32* SOCU_buffer = NULL;
#endif

#define BFTPS_MAX_CONNECTIONS 4
#define BFTPS_PORT_LISTEN 5000

typedef enum {
    BFTPS_MODE_INVALID,
    BFTPS_MODE_STARTING,
    BFTPS_MODE_LISTENING,
    BFTPS_MODE_STOPPING,
    BFTPS_MODE_RESTARTING
} bftps_mode_t;

typedef struct {
    bftps_mode_t mode;
    thread_handle_t thread;
    event_handle_t event;
    short sessionsNumber;
    time_t startTime;
#ifdef _3DS
    bool socInit;
#endif
    bftps_session_context_t sessions[BFTPS_MAX_CONNECTIONS];
} bftps_context_t;

THREAD_CALLBACK_DEFINITION(bftps_worker_thread, arg) {
    int nErrorCode = ENOSYS;
    bftps_context_t* context = (bftps_context_t*) arg;
BFTPS_WORKER_THREAD_RESTARTING:
    // set all sessions fd to -1
    context->mode = BFTPS_MODE_STARTING;
    context->startTime = time(NULL);
    int i;
    for (i = 0; i < BFTPS_MAX_CONNECTIONS; ++i) {
        context->sessions[i].commandFd = -1;
        context->sessions[i].mode = BFTPS_SESSION_MODE_INVALID;
    }
    int fdListen = 0; // define here before any goto error cleanup
#ifdef _3DS
    // allocate buffer for SOC service
    if (NULL == SOCU_buffer) {
        SOCU_buffer = (u32*) memalign(SOCU_ALIGN, SOCU_BUFFERSIZE);
        if (NULL == SOCU_buffer) {
            nErrorCode = ENOMEM;
            CONSOLE_LOG("memalign: failed to allocate SOCU_buffer");
            goto BFTPS_WORKER_THREAD_ERROR_CLEANUP;
        }
    }

    // initialize SOC service
    if (R_FAILED(nErrorCode = socInit(SOCU_buffer, SOCU_BUFFERSIZE))) {
        CONSOLE_LOG("socInit: %08X\n", (unsigned int) nErrorCode);
        goto BFTPS_WORKER_THREAD_ERROR_CLEANUP;
    } else
        context->socInit = true;
#endif

    // allocate socket to listen for clients
    // I will not create the socket as SOCK_NONBLOCK for now I will use the 
    // flag on the send command
    fdListen = socket(AF_INET, SOCK_STREAM, 0);
    if (0 > fdListen) {
        nErrorCode = errno;
        CONSOLE_LOG("Failed to create socket to listen: %d", nErrorCode);
        goto BFTPS_WORKER_THREAD_ERROR_CLEANUP;
    }
    // reuse the same address
    int enable = 1;
    if (0 > setsockopt(fdListen, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof (int))) {
        nErrorCode = errno;
        CONSOLE_LOG("Failed to set reuse address option on listen socket: %d", nErrorCode);
        goto BFTPS_WORKER_THREAD_ERROR_CLEANUP;
    }
    // server listen address
    static struct sockaddr_in bftpsAddress;
    bftpsAddress.sin_family = AF_INET;
#ifdef __linux__
    bftpsAddress.sin_addr.s_addr = INADDR_ANY;
#elif _3DS
    bftpsAddress.sin_addr.s_addr = gethostid();
#endif
    bftpsAddress.sin_port = htons(BFTPS_PORT_LISTEN);

    // bind socket to listen address
    if (0 > bind(fdListen, (struct sockaddr*) &bftpsAddress,
            sizeof (bftpsAddress))) {
        nErrorCode = errno;
        CONSOLE_LOG("Failed to bind listen socket: %d", nErrorCode);
        goto BFTPS_WORKER_THREAD_ERROR_CLEANUP;
    }
    // listen on socket
    if (0 > listen(fdListen, BFTPS_MAX_CONNECTIONS)) {
        nErrorCode = errno;
        CONSOLE_LOG("Failed to listen: %d", nErrorCode);
        goto BFTPS_WORKER_THREAD_ERROR_CLEANUP;
    }

    CONSOLE_LOG("Listening on: %s:%u", inet_ntoa(bftpsAddress.sin_addr),
            ntohs(bftpsAddress.sin_port));

    // change the mode to listening and set the event to sync with caller thread
    context->mode = BFTPS_MODE_LISTENING;
    event_set(context->event);
    // we will listen forever until server mode changes state
    //for not be so cpu intensive, when no data transfers connections 
    //are available we will wait longer on poll, otherwise don't wait
    int pollTime = 150;
    while (context->mode == BFTPS_MODE_LISTENING) {
        // we will poll for new client connections
        struct pollfd fds[1];
        fds[0].fd = fdListen;
        fds[0].events = POLLIN;
        fds[0].revents = 0;
        // poll for a new connection
        int result = poll(fds, 1, pollTime);
        if (0 > result) {
            nErrorCode = errno;
            if (nErrorCode == ENETDOWN) { // wifi got disabled, so let's restart
                context->mode = BFTPS_MODE_RESTARTING;
            }
            else {
                CONSOLE_LOG("Failed to poll listen socket: %d", nErrorCode);
            }
            goto BFTPS_WORKER_THREAD_ERROR_CLEANUP;
        } else if (0 < result) {
            // we have a new client 
            int session;
            for (session = 0; session < BFTPS_MAX_CONNECTIONS; ++session) {
                if (context->sessions[session].mode == BFTPS_SESSION_MODE_INVALID) {
                    bftps_session_init(&context->sessions[session], fdListen);
                    break;
                }
            }
        }

        // let's do some work on the connected sockets
        pollTime = 150;
        int session;
        for (session = 0; session < BFTPS_MAX_CONNECTIONS; ++session) {
            if (context->sessions[session].mode != BFTPS_SESSION_MODE_INVALID) {
                int result = 0;
                if (FAILED(result = bftps_session_poll(&context->sessions[session]))) {
                    // check if we only need to try again
                    if (result != EAGAIN && result != EWOULDBLOCK) {
                        CONSOLE_LOG("Failed to poll: %d %s", result, strerror(result));
                        // mark this session to be destroyed
                        bftps_session_mode_set(&context->sessions[session],
                                BFTPS_SESSION_MODE_DESTROY, 0);
                    } else
                        pollTime = 0;
                } else if (context->sessions[session].mode == BFTPS_SESSION_MODE_DATA_TRANSFER ||
                        context->sessions[session].mode == BFTPS_SESSION_MODE_DATA_CONNECT)
                    pollTime = 0;
            }
        }
    }

BFTPS_WORKER_THREAD_ERROR_CLEANUP:

#ifdef _3DS
    if (true == context->socInit) {
        socExit();
        context->socInit = false;
    }
#endif
    if (0 <= fdListen) {
        // close all sessions first
        int i;
        for (i = 0; i < BFTPS_MAX_CONNECTIONS; ++i)
            bftps_session_destroy(&context->sessions[i]);

        bftps_socket_destroy(&fdListen, false);
    }
    // we restart the server if needed
    if (context->mode == BFTPS_MODE_RESTARTING) {
        CONSOLE_LOG("Restarting server");
        goto BFTPS_WORKER_THREAD_RESTARTING;
    }
    // set the event to sync with caller thread
    event_set(context->event);
    context->mode = BFTPS_MODE_INVALID;
    context->startTime = 0;

    THREAD_CALLBACK_RETURN(nErrorCode);
}

static bftps_context_t bftpsContext = {
    BFTPS_MODE_INVALID, // mode
    NULL, // thread
    NULL, // event
    0, // sessionsNumber
    0, // startTime
#ifdef _3DS
    false,
#endif
    {0} // sessions
};

int bftps_start() {
    if (bftpsContext.mode != BFTPS_MODE_INVALID)
        return EALREADY;

    int nErrorCode = 0;

    // create an event so we can sync thread operations
    if (FAILED(nErrorCode = event_create(&bftpsContext.event)))
        goto BFTPS_START_ERROR_CLEANUP;
    // create the worker thread 
    if (FAILED(nErrorCode = thread_create(&bftpsContext.thread,
            bftps_worker_thread, &bftpsContext)))
        goto BFTPS_START_ERROR_CLEANUP;
    // await for an event to be set to sync operations
    if (FAILED(nErrorCode = event_wait(bftpsContext.event, INT_MAX)))
        goto BFTPS_START_ERROR_CLEANUP;
    // last check to see if ftp is listening
    if (bftpsContext.mode != BFTPS_MODE_LISTENING)
        goto BFTPS_START_ERROR_CLEANUP;

    return nErrorCode;

BFTPS_START_ERROR_CLEANUP:
    if (bftpsContext.event)
        event_destroy(&bftpsContext.event);

    if (bftpsContext.thread) {
        thread_exit_code_t exitCode = 0;
        thread_join(&bftpsContext.thread, &exitCode);
        if (exitCode) // if thread failed initializing something let's return it
            nErrorCode = (long) exitCode;
    }
    bftpsContext.mode = BFTPS_MODE_INVALID;

    return nErrorCode;
}

int bftps_stop() {
    if (bftpsContext.mode != BFTPS_MODE_LISTENING &&
            bftpsContext.mode != BFTPS_MODE_STARTING)
        return 0;
    // change the mode of server so it stops the worker thread cycle
    bftpsContext.mode = BFTPS_MODE_STOPPING;
    // wait for the worker thread to die
    thread_join(&bftpsContext.thread, NULL);
    // reset server context so it can be started again
    event_destroy(&bftpsContext.event);
    bftpsContext.mode = BFTPS_MODE_INVALID;

    return 0;
}

time_t bftps_start_time() {
    return bftpsContext.startTime;
}

bool bftps_exiting() {
    return bftpsContext.mode == BFTPS_MODE_STOPPING;
}