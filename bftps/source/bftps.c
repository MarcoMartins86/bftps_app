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
#include "atomic.h"

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

typedef enum {
    FILE_TRANSFER_INVALID,
    FILE_TRANSFER_STORED,
    FILE_TRANSFER_STORING,
    FILE_TRANSFER_READ,
    FILE_TRANSFER_READING,
} bftps_file_transfer_wrapper_mode_t;

typedef struct _bftps_file_transfer_ext_t {
    bftps_file_transfer_mode_t mode;
    unsigned int fileSize; // This is only valid for sending files
    unsigned int filePosition;
    struct _bftps_file_transfer_ext_t* next;
    char name[MAX_PATH];
    bftps_session_context_t* id; // we will use the file transfer session as ID
} bftps_file_transfer_ext_t;

typedef struct {
    bftps_file_transfer_ext_t* fileTransfer;
    bftps_file_transfer_wrapper_mode_t mode;
} bftps_file_transfer_wrapper_t; 

typedef struct {
    bftps_mode_t mode;
    thread_handle_t thread;
    event_handle_t event;
    time_t startTime;
    char name[256];
#ifdef _3DS
    bool socInit;
#endif
    bool wroteFileTransfer;
    bftps_session_context_t *sessions;
    bftps_file_transfer_wrapper_t fileTransferBuffer[2];
    unsigned int readIndex;
    unsigned int writeIndex;
} bftps_context_t;

THREAD_CALLBACK_DEFINITION(bftps_worker_thread, arg) {
    int nErrorCode = 0;
    bftps_context_t* context = (bftps_context_t*) arg;
BFTPS_WORKER_THREAD_RESTARTING:
    context->mode = BFTPS_MODE_STARTING;
    context->startTime = time(NULL);
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
    socklen_t addrlen = sizeof(bftpsAddress);
    bftpsAddress.sin_family = AF_INET;
#ifdef __linux__
    bftpsAddress.sin_addr.s_addr = INADDR_ANY;
#elif _3DS
    bftpsAddress.sin_addr.s_addr = gethostid();
#endif
    bftpsAddress.sin_port = htons(BFTPS_PORT_LISTEN);

    // bind socket to listen address
    if (0 > bind(fdListen, (struct sockaddr*) &bftpsAddress, addrlen)) {
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

#ifdef _3DS
    snprintf(context->name, sizeof (context->name), "Listening on: %s:%u",
            inet_ntoa(bftpsAddress.sin_addr), ntohs(bftpsAddress.sin_port));
#elif __linux__
    char hostname[128];
    if (0 != gethostname(hostname, sizeof (hostname))) {
        CONSOLE_LOG("gethostname: %d %s", errno, strerror(errno));
    } else {
        snprintf(context->name, sizeof (context->name), "Listening on: %s:%u",
                hostname, ntohs(bftpsAddress.sin_port));
    }
#endif
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
            } else {
                CONSOLE_LOG("Failed to poll listen socket: %d", nErrorCode);
            }
            goto BFTPS_WORKER_THREAD_ERROR_CLEANUP;
        } else if (0 < result) {
            // we have a new client, so let's find the place to create the new session        
            bftps_session_context_t ** pNewSession = &context->sessions;
            while (NULL != *pNewSession)
                pNewSession = &(*pNewSession)->next;
            // and now create it
            bftps_session_init(pNewSession, fdListen);
        }

        // let's do some work on the connected sockets
        pollTime = 150; // restore the poll time to the original value
        bftps_session_context_t* sessionToWork = context->sessions;
        bftps_session_context_t* previousSession = NULL;
        while (NULL != sessionToWork) {
            // first check if it is to destroy the session
            if (sessionToWork->mode == BFTPS_SESSION_MODE_DESTROY) {
                // save the pointer to next session
                bftps_session_context_t* next = sessionToWork->next;
                bftps_session_destroy(sessionToWork);
                // restore the pointer to next session
                if (previousSession)
                    previousSession->next = next;
                else
                    context->sessions = next;
                // update the variable to point to next session
                sessionToWork = next;
            } else {
                int result = 0;
                if (FAILED(result = bftps_session_poll(sessionToWork))) {
                    // check if we only need to try again
                    if (result != EAGAIN && result != EWOULDBLOCK) {
                        CONSOLE_LOG("Failed to poll: %d %s", result, strerror(result));
                        // mark this session to be destroyed
                        bftps_session_mode_set(sessionToWork,
                                BFTPS_SESSION_MODE_DESTROY, 0);
                    } else
                        pollTime = 0; // so can try again right away
                } else if (sessionToWork->mode == BFTPS_SESSION_MODE_DATA_TRANSFER ||
                        sessionToWork->mode == BFTPS_SESSION_MODE_DATA_CONNECT)
                    pollTime = 0; // so we don't delay when transferring data
                //update the variables to point to next session
                previousSession = sessionToWork;
                sessionToWork = sessionToWork->next;
            }
        }
    }

BFTPS_WORKER_THREAD_ERROR_CLEANUP:
    CONSOLE_LOG("Destroying server");
#ifdef _3DS
    if (true == context->socInit) {
        socExit();
        context->socInit = false;
    }
#endif
    if (0 <= fdListen) {
        // close all sessions first
        bftps_session_context_t *session = context->sessions;
        while (NULL != session) {
            // this will close all open sockets
            bftps_session_mode_set(session, BFTPS_SESSION_MODE_DESTROY, 0);
            // save next session pointer
            bftps_session_context_t* next = session->next;
            // this will free the memory
            bftps_session_destroy(session);
            // update the variable to mode to next session
            session = next;
        }
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

    THREAD_CALLBACK_RETURN(nErrorCode);
}

static bftps_context_t* gp_bftpsContext = NULL; /*{
    BFTPS_MODE_INVALID, // mode
    NULL, // thread
    NULL, // event
    0, // sessionsNumber
    0, // startTime
#ifdef _3DS
    false,
#endif
    {0} // sessions
};*/

int bftps_start() {
    CONSOLE_LOG("Start server");
    // make sure we haven't started already
    if (NULL != gp_bftpsContext)
        return EALREADY;
    // allocate memory for the context
    gp_bftpsContext = malloc(sizeof (bftps_context_t));
    if (NULL == gp_bftpsContext)
        return ENOMEM;

    // set context default values
    gp_bftpsContext->mode = BFTPS_MODE_INVALID;
    gp_bftpsContext->event = NULL;
    gp_bftpsContext->thread = NULL;
    gp_bftpsContext->startTime = 0;
    gp_bftpsContext->name[0] = '\0';
    gp_bftpsContext->sessions = NULL;
    gp_bftpsContext->fileTransferBuffer[0].fileTransfer = NULL;
    gp_bftpsContext->fileTransferBuffer[0].mode = FILE_TRANSFER_INVALID;
    gp_bftpsContext->fileTransferBuffer[1].fileTransfer = NULL;
    gp_bftpsContext->fileTransferBuffer[1].mode = FILE_TRANSFER_INVALID;
    gp_bftpsContext->readIndex = 0;
    gp_bftpsContext->writeIndex =1;
    gp_bftpsContext->wroteFileTransfer = false;

    int nErrorCode = 0;

    // create an event so we can sync with the worker thread
    if (FAILED(nErrorCode = event_create(&gp_bftpsContext->event)))
        goto BFTPS_START_ERROR_CLEANUP;
    // create the worker thread, that will be doing all the FTP work
    if (FAILED(nErrorCode = thread_create(&gp_bftpsContext->thread,
            bftps_worker_thread, gp_bftpsContext)))
        goto BFTPS_START_ERROR_CLEANUP;
    // await for the event to be set inside the worker thread, so we know
    // that we are ready to continue or an error occurred
    if (FAILED(nErrorCode = event_wait(gp_bftpsContext->event, INT_MAX)))
        goto BFTPS_START_ERROR_CLEANUP;
    // last check to see if ftp is listening or error occurred
    if (gp_bftpsContext->mode != BFTPS_MODE_LISTENING) {
        nErrorCode = EAGAIN;
        goto BFTPS_START_ERROR_CLEANUP;
    }

    return nErrorCode;

BFTPS_START_ERROR_CLEANUP:
    if (gp_bftpsContext->event)
        event_destroy(&gp_bftpsContext->event);

    if (gp_bftpsContext->thread)
        thread_join(&gp_bftpsContext->thread, NULL);

    free(gp_bftpsContext);
    gp_bftpsContext = NULL;

    return nErrorCode;
}

int bftps_stop() {
    // check that the FTP worker thread is running
    if (NULL == gp_bftpsContext)
        return 0;

    // change the mode of server so it stops the worker thread cycle
    gp_bftpsContext->mode = BFTPS_MODE_STOPPING;
    // wait for the worker thread to die
    thread_join(&gp_bftpsContext->thread, NULL);
    // free the remaining allocated memory
    event_destroy(&gp_bftpsContext->event);
    {
        bftps_file_transfer_ext_t* pFree = gp_bftpsContext->fileTransferBuffer[0].fileTransfer;
        bftps_file_transfer_ext_t* next = NULL;
        while (pFree) {
            next = pFree->next;
            free(pFree);
            pFree = next;
        }
    }
    {
        bftps_file_transfer_ext_t* pFree = gp_bftpsContext->fileTransferBuffer[1].fileTransfer;
        bftps_file_transfer_ext_t* next = NULL;
        while (pFree) {
            next = pFree->next;
            free(pFree);
            pFree = next;
        }
    }    
    free(gp_bftpsContext);
    gp_bftpsContext = NULL;
    CONSOLE_LOG("Stop server");
    return 0;
}

const char* bftps_name() {
    if(gp_bftpsContext)
        return gp_bftpsContext->name;
    else
        return "";
}

time_t bftps_start_time() {
    if (gp_bftpsContext)
        return gp_bftpsContext->startTime;
    else
        return 0;
}

bool bftps_exiting() {
    if (gp_bftpsContext)
        return gp_bftpsContext->mode == BFTPS_MODE_STOPPING;
    else
        return false;
}

void bftps_file_transfer_store_internal(bftps_session_context_t* session,
        bftps_file_transfer_wrapper_t* file_transfer_wrapper) {
    
    // try to find this file transfer info to refresh with new values or create another
    bftps_file_transfer_ext_t** fileTransfer = &file_transfer_wrapper->fileTransfer;
    while (*fileTransfer) {
        // we found it
        if ((*fileTransfer)->id == session)
            break;
        // continue looking
        fileTransfer = &(*fileTransfer)->next;
    }
    // check if we need to allocate new memory
    bool bAllocatedNow = false;
    if (NULL == (*fileTransfer)) {
        *fileTransfer = malloc(sizeof(bftps_file_transfer_ext_t));
        session->filenameRefresh = true; // to make sure we copy all values
        bAllocatedNow = true;
    }
    
    // recheck if the address is valid or not
    if (*fileTransfer) {
        // if it was just allocated we will initialize the next object to NULL
        // and set its id
        if(bAllocatedNow) {
            (*fileTransfer)->id = session;
            (*fileTransfer)->next = NULL;
        }
        
        // if we need to refresh the file name we also need to refresh all other values
        if (session->filenameRefresh) {
            // the file name will always be on this buffer
            strncpy((*fileTransfer)->name, session->filename, sizeof((*fileTransfer)->name));
            
            (*fileTransfer)->mode = session->flags & BFTPS_SESSION_FLAG_SEND ?
                    FILE_SENDING : FILE_RECEIVING;

            (*fileTransfer)->fileSize = session->filesize;
            (*fileTransfer)->filePosition = session->filepos;

            session->filenameRefresh = false;
        } else { // we only need to refresh the file position
            (*fileTransfer)->filePosition = session->filepos;
        }
    }
}

void bftps_file_transfer_store(bftps_session_context_t* session) {
    if (gp_bftpsContext) {
        unsigned int index = gp_bftpsContext->writeIndex;
        if (atomic_compare_swap(&gp_bftpsContext->fileTransferBuffer[index].mode,
                FILE_TRANSFER_STORED, FILE_TRANSFER_STORING) ||
                atomic_compare_swap(&gp_bftpsContext->fileTransferBuffer[index].mode,
                FILE_TRANSFER_READ, FILE_TRANSFER_STORING) ||
                atomic_compare_swap(&gp_bftpsContext->fileTransferBuffer[index].mode,
                FILE_TRANSFER_INVALID, FILE_TRANSFER_STORING)) {
            bftps_file_transfer_store_internal(session, &gp_bftpsContext->fileTransferBuffer[index]);
            // restore the state
            gp_bftpsContext->fileTransferBuffer[index].mode = FILE_TRANSFER_STORED;
            gp_bftpsContext->wroteFileTransfer = true;
        }
    }
}

void bftps_file_transfer_remove_internal(bftps_session_context_t* session,
        bftps_file_transfer_wrapper_t* file_transfer_wrapper) {
    bftps_file_transfer_ext_t** fileTransfer = &file_transfer_wrapper->fileTransfer;
    while (*fileTransfer) {
        // we found it
        if ((*fileTransfer)->id == session) {
            // save the pointer to the next one
            bftps_file_transfer_ext_t* next = (*fileTransfer)->next;
            // free it
            free(*fileTransfer);
            // restore the pointer to the next
            *fileTransfer = next;
            break;
        }
        // continue looking
        fileTransfer = &(*fileTransfer)->next;
    }
}

void bftps_file_transfer_remove(bftps_session_context_t* session) {
    if (gp_bftpsContext) {
        // we will only remove when stored
        unsigned int index = gp_bftpsContext->writeIndex;
        if (atomic_compare_swap(&gp_bftpsContext->fileTransferBuffer[index].mode,
                FILE_TRANSFER_STORED, FILE_TRANSFER_STORING)) {
            bftps_file_transfer_store_internal(session, &gp_bftpsContext->fileTransferBuffer[index]);
            // restore the state
            gp_bftpsContext->fileTransferBuffer[index].mode = FILE_TRANSFER_STORED;
        }
    }
}

const bftps_file_transfer_t* bftps_file_transfer_retrieve_internal(
        bftps_file_transfer_wrapper_t* file_transfer_wrapper) {

    bftps_file_transfer_t* pReturn = NULL;
    bftps_file_transfer_ext_t* fileTransfer = file_transfer_wrapper->fileTransfer;
    bftps_file_transfer_t** fileTransferReturn = &pReturn;
    while (fileTransfer) {
        // allocate space for this file transfer info
        *fileTransferReturn = malloc(sizeof (bftps_file_transfer_t));
        // check that the allocation was successful
        if (*fileTransferReturn) {
            // copy the memory
            memcpy(*fileTransferReturn, fileTransfer, sizeof (bftps_file_transfer_t));
            // make sure next pointer, points to NULL
            (*fileTransferReturn)->next = NULL;
            // refresh the variable to be used in next iteration
            fileTransferReturn = &(*fileTransferReturn)->next;
        }
        // save the pointer
        bftps_file_transfer_ext_t* next = fileTransfer->next;
        // free the memory
        free(fileTransfer);
        // move on to the next one
        fileTransfer = next;
    }

    file_transfer_wrapper->fileTransfer = NULL; // all memory was freed

    return pReturn;
}

const bftps_file_transfer_t* bftps_file_transfer_retrieve() {
    const bftps_file_transfer_t* pReturn = NULL;
    if (gp_bftpsContext) {
        // only continue if we have something to read
        if (atomic_compare_swap(&gp_bftpsContext->wroteFileTransfer, true, false)) {
            unsigned int index = gp_bftpsContext->readIndex;

            if (atomic_compare_swap(&gp_bftpsContext->fileTransferBuffer[index].mode,
                    FILE_TRANSFER_STORED, FILE_TRANSFER_READING)) {
                pReturn = bftps_file_transfer_retrieve_internal(&gp_bftpsContext->fileTransferBuffer[index]);
                // restore the state
                gp_bftpsContext->fileTransferBuffer[index].mode = FILE_TRANSFER_READ;
                
                // even though we are reading we will set the variable again because
                // there could be something to read on the other buffer although we
                // did not write anything between calls
                gp_bftpsContext->wroteFileTransfer = true;
            }
            
            // swap buffers
            if (1 == index) {
                gp_bftpsContext->writeIndex = 1;
                gp_bftpsContext->readIndex = 0;
            } else {
                gp_bftpsContext->writeIndex = 0;
                gp_bftpsContext->readIndex = 1;
            }
        }
    }
    return pReturn;
}

void bftps_file_transfer_cleanup(const bftps_file_transfer_t* file_transfer) {
    bftps_file_transfer_t* fileTransferDelete = (bftps_file_transfer_t*)file_transfer;
    while (fileTransferDelete) {
        bftps_file_transfer_t *next = fileTransferDelete->next;
        free(fileTransferDelete);
        fileTransferDelete = next;
    }
}