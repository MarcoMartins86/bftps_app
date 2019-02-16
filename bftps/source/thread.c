#include <errno.h>
#ifdef _3DS
#include <3ds.h>
#define STACKSIZE (4 * 1024)
#endif

#include "thread.h"
#include "macros.h"

int thread_create(thread_handle_t* thread, thread_routine_t start_routine,
        void *arg) {
    if (!thread || *thread || !start_routine)
        return EINVAL;

    int nErrorCode = ENOSYS;

#ifdef __linux__
    nErrorCode = pthread_create(thread, NULL, start_routine, arg);
#elif _3DS
    s32 prio = 0;
    if (R_SUCCEEDED(nErrorCode = svcGetThreadPriority(&prio, CUR_THREAD_HANDLE))) {
        Thread newThread = threadCreate(start_routine, arg, STACKSIZE, prio - 1, -1, false);
        if (newThread) {
            *thread = newThread;
            nErrorCode = 0;
        } else
            nErrorCode = ENOMEM; // not sure what error to give
    }
#endif   

    return nErrorCode;
}

int thread_join(thread_handle_t* thread, thread_exit_code_t* exit_code) {
    if (!thread || !(*thread))
        return EINVAL;

    int nErrorCode = ENOSYS;
#ifdef __linux__
    nErrorCode = pthread_join(*thread, exit_code);
#elif _3DS
    threadJoin(*thread, U64_MAX);
    threadFree(*thread);
    nErrorCode = 0;
    if (exit_code)
        *exit_code = 0;
#endif   

    *thread = NULL;

    return nErrorCode;
}