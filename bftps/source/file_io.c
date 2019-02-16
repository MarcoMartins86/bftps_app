#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <sys/socket.h>

#include "file_io.h"
#include "time.h"
#include "macros.h"

#ifdef __linux__
#define BUFFER_SIZE 1048576 // 1 MB
#define BUFFER_COUNT 12 // 
#elif   _3DS
#define BUFFER_SIZE 32768 // 
#define BUFFER_COUNT 2 // 
#endif


typedef struct {
    unsigned char buffer[BUFFER_COUNT][BUFFER_SIZE];
    int bytes[BUFFER_COUNT];
    thread_handle_t threadRead;
    thread_handle_t threadWrite;
    event_handle_t eventThreadAwakeRead;
    event_handle_t eventThreadAwakeWrite;
    event_handle_t eventThreadSleepRead;
    event_handle_t eventThreadSleepWrite;
    bool threadExit;
    unsigned short readIndex;
    unsigned short readTurns;
    unsigned short writeIndex;
    unsigned short writeTurns;
    int readFd;
    int writeFd;
    file_io_mode_t mode;
} _file_io_context_t;

THREAD_CALLBACK_DEFINITION(file_io_worker_thread_read, arg) {
    _file_io_context_t* context = (_file_io_context_t*) arg;
    // wait for thread awake signal and check if it is to exit
    while (SUCCEEDED(event_wait(context->eventThreadAwakeRead, INT_MAX)) &&
            !context->threadExit) {
        // just a failsafe
        if (context->mode == file_io_mode_read ||
                context->mode == file_io_mode_read_write) {
            //inner loop for reading all the data
            while (!context->threadExit) {
                // since this values will suffer from thread concurrency and
                // I don't want to use a sync method to slow down things
                // this should suffice
                short writeTurns = context->writeTurns;
                short writeIndex = context->writeIndex;
                // check if we can read, we don't want to overlap with write
                if ((context->readTurns > writeTurns &&
                        context->readIndex < writeIndex) ||
                        (context->readTurns == writeTurns &&
                        context->readIndex >= writeIndex)) {
                    // read the data and save the number of bytes read, so later 
                    // on we know for sure how many bytes we will write
                    context->bytes[context->readIndex] = read(context->readFd,
                            context->buffer[context->readIndex],
                            BUFFER_SIZE);
                    // we have reached to end of file or error occurred
                    if (context->bytes[context->readIndex] <= 0)
                        break;
                    // check if we need to increase a turn and reset the index
                    if (BUFFER_COUNT == ++context->readIndex) {
                        context->readIndex = 0;
                        ++context->readTurns;
                    }
                } else {
                    // give some time to the write operation to catch up
                    time_sleep(500);
                }
            }
            // this thread will now entering in sleep mode
            event_set(context->eventThreadSleepRead);
        }
        // when code reaches here we reset the awake signal or exit
        if (!context->threadExit)
            event_reset(context->eventThreadAwakeRead);
    }
    THREAD_CALLBACK_RETURN(0);
}

THREAD_CALLBACK_DEFINITION(file_io_worker_thread_write, arg) {
    _file_io_context_t* context = (_file_io_context_t*) arg;

    // wait for thread awake signal and check if it is to exit
    while (SUCCEEDED(event_wait(context->eventThreadAwakeWrite, INT_MAX)) &&
            !context->threadExit) {
        // just a failsafe
        if (context->mode == file_io_mode_write ||
                context->mode == file_io_mode_read_write) {
            bool needConfirmation = true;
            //inner loop for writing all the data
            while (!context->threadExit) {
                // check if we can write, we don't want to overlap with read
                if (context->readTurns > context->writeTurns ||
                        (context->readTurns == context->writeTurns &&
                        context->readIndex > context->writeIndex)) {
                    needConfirmation = true; // still data to read so clear flag
                    int bytesWritten = write(context->writeFd,
                            context->buffer[context->writeIndex],
                            context->bytes[context->writeIndex]);
                    /*int bytesWritten = send(context->writeFd, 
                            context->buffer[context->writeIndex], 
                            context->bytes[context->writeIndex], 0);*/
                    // error occurred
                    if (bytesWritten < context->bytes[context->writeIndex])
                        break;
                    // check if we need to increase a turn and reset the index
                    if (BUFFER_COUNT == ++context->writeIndex) {
                        context->writeIndex = 0;
                        ++context->writeTurns;
                    }
                } else if (context->readTurns == context->writeTurns &&
                        context->readIndex == context->writeIndex &&
                        SUCCEEDED(event_wait(context->eventThreadSleepRead, 0))) {
                    // this forces to confirm that the read buffer was not 
                    // updated due to thread concurrency and avoids using sync
                    if (needConfirmation) {
                        needConfirmation = false;
                    } else {
                        //the last piece to write
                        int bytesWritten = write(context->writeFd,
                                context->buffer[context->writeIndex],
                                context->bytes[context->writeIndex]);
                        break;
                    }
                } else {
                    // give some time to the read operation to catch up
                    time_sleep(500);
                }
            }
            // this thread will now entering in sleep mode
            event_set(context->eventThreadSleepWrite);
        }
        // when code reaches here we reset the awake signal or exit
        if (!context->threadExit)
            event_reset(context->eventThreadAwakeWrite);
    }
    THREAD_CALLBACK_RETURN(0);
}

int file_io_init(file_io_context_t** operation_context,
        file_io_mode_t mode, int read_fd, int write_fd) {
    if (!operation_context || *operation_context || (mode == file_io_mode_invalid))
        return EINVAL;

    int nErrorCode = 0;

    _file_io_context_t* context = malloc(sizeof (_file_io_context_t));
    if (!context)
        nErrorCode = errno;
    else {
        // init variables with default values
        context->eventThreadAwakeRead = NULL;
        context->eventThreadAwakeWrite = NULL;
        context->eventThreadSleepRead = NULL;
        context->eventThreadSleepWrite = NULL;
        context->threadRead = NULL;
        context->threadWrite = NULL;
        context->threadExit = false;
        context->readIndex = 0;
        context->readTurns = 0;
        context->writeIndex = 0;
        context->writeTurns = 0;
        context->readFd = read_fd;
        context->writeFd = write_fd;
        context->mode = mode;

        //create the needed events
        if (FAILED(nErrorCode = event_create(&context->eventThreadAwakeRead)))
            goto FILE_IO_INIT_ERROR_CLEANUP;
        if (FAILED(nErrorCode = event_create(&context->eventThreadAwakeWrite)))
            goto FILE_IO_INIT_ERROR_CLEANUP;
        if (FAILED(nErrorCode = event_create(&context->eventThreadSleepRead)))
            goto FILE_IO_INIT_ERROR_CLEANUP;
        if (FAILED(nErrorCode = event_create(&context->eventThreadSleepWrite)))
            goto FILE_IO_INIT_ERROR_CLEANUP;
        // create the threads
        if (FAILED(nErrorCode = thread_create(&context->threadRead,
                file_io_worker_thread_read, context)))
            goto FILE_IO_INIT_ERROR_CLEANUP;
        if (FAILED(nErrorCode = thread_create(&context->threadWrite,
                file_io_worker_thread_write, context)))
            goto FILE_IO_INIT_ERROR_CLEANUP;
        // everything went ok so lets return the context to the user
        *operation_context = (file_io_context_t*) context;
    }

    return nErrorCode;

FILE_IO_INIT_ERROR_CLEANUP: // let's cleanup the context since an error occurred

    file_io_destroy((file_io_context_t**) & context);

    return nErrorCode;
}

int file_io_start_read(file_io_context_t* operation_context) {
    if (!operation_context)
        return EINVAL;

    _file_io_context_t* context = (_file_io_context_t*) operation_context;

    return event_set(context->eventThreadAwakeRead);
}

int file_io_start_write(file_io_context_t* operation_context) {
    if (!operation_context)
        return EINVAL;

    _file_io_context_t* context = (_file_io_context_t*) operation_context;

    return event_set(context->eventThreadAwakeWrite);
}

int file_io_wait(file_io_context_t* operation_context, int timeout_ms) {
    _file_io_context_t* context = (_file_io_context_t*) operation_context;

    int nErrorCode = event_wait(context->eventThreadSleepWrite, timeout_ms);
    if (SUCCEEDED(nErrorCode)) {
        event_reset(context->eventThreadSleepRead);
        event_reset(context->eventThreadSleepWrite);
    }

    return nErrorCode;
}

void file_io_destroy(file_io_context_t** operation_context) {
    if (!operation_context || !(*operation_context))
        return;

    _file_io_context_t* context = (_file_io_context_t*) * operation_context;

    // we will set the flag and awake the threads to exit
    context->threadExit = true;
    if (context->eventThreadAwakeRead)
        event_set(context->eventThreadAwakeRead);
    if (context->eventThreadAwakeWrite)
        event_set(context->eventThreadAwakeWrite);
    // now we will wait for threads to exit
    if (context->threadRead)
        thread_join(&context->threadRead, NULL);
    if (context->threadWrite)
        thread_join(&context->threadWrite, NULL);
    // now free the memory
    if (context->eventThreadAwakeRead)
        event_destroy(&context->eventThreadAwakeRead);
    if (context->eventThreadAwakeWrite)
        event_destroy(&context->eventThreadAwakeWrite);
    if (context->eventThreadSleepRead)
        event_destroy(&context->eventThreadSleepRead);
    if (context->eventThreadSleepWrite)
        event_destroy(&context->eventThreadSleepWrite);

    free(context);
    *operation_context = NULL;
}

