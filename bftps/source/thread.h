#ifndef THREAD_H
#define THREAD_H

// include the needed headers per systems
#ifdef __linux__
#include<pthread.h>
#elif _3DS
#include <3ds.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

    // define the thread handle per systems
#ifdef __linux__
    typedef pthread_t thread_handle_t;
    typedef void* thread_exit_code_t;
    typedef void *(*thread_routine_t) (void *);
#define THREAD_CALLBACK_DEFINITION(name, argument) void* name(void *argument)
#define THREAD_CALLBACK_RETURN(value) return (void*)(long)value;
#elif _3DS
    typedef Thread thread_handle_t;
    typedef void* thread_exit_code_t;
    typedef void (*thread_routine_t) (void *);
#define THREAD_CALLBACK_DEFINITION(name, argument) void name(void *argument)
#define THREAD_CALLBACK_RETURN(value)
#endif

    extern int thread_create(thread_handle_t* thread, thread_routine_t callback,
            void *arg);

    extern int thread_join(thread_handle_t* thread, thread_exit_code_t* exit_code);

#ifdef __cplusplus
}
#endif

#endif /* THREAD_H */

