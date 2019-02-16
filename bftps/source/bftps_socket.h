#ifndef BFPTS_SOCKET_H
#define BFPTS_SOCKET_H

#include "bool.h"

#ifdef __cplusplus
extern "C" {
#endif

    #define BFTPS_SOCKET_BUFFER_SIZE 32768
    extern int bftps_socket_options_increase_buffers(int fd);
    extern int bftps_socket_destroy(int* p_fd, bool session_socket);


#ifdef __cplusplus
}
#endif

#endif /* BFPTS_SOCKET_H */

