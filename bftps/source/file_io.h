#ifndef FILE_IO_H
#define FILE_IO_H

#include "thread.h"
#include "event.h"
#include "bool.h"

#ifdef __cplusplus
extern "C" {
#endif
    typedef enum {
        file_io_mode_invalid,
        file_io_mode_read,
        file_io_mode_write,
        file_io_mode_read_write
    } file_io_mode_t;
    
    typedef struct _file_io_context_t file_io_context_t;
    
    extern int file_io_init(file_io_context_t** operation_context, 
        file_io_mode_t mode, int read_fd, int write_fd);
    extern int file_io_start_read(file_io_context_t* operation_context);
    extern int file_io_start_write(file_io_context_t* operation_context);
    extern int file_io_wait(file_io_context_t* operation_context, int timeout_ms);
    extern void file_io_destroy(file_io_context_t** operation_context);
    

#ifdef __cplusplus
}
#endif

#endif /* FILE_IO_H */

