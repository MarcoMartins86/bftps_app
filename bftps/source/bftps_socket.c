#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <unistd.h>

#include "bftps_socket.h"
#include "macros.h"

int bftps_socket_options_increase_buffers(int fd) {
    static int sockBufferSize = BFTPS_SOCKET_BUFFER_SIZE;
    int nErrorCode = 0;
    // increase receive buffer size
    if (0 != setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &sockBufferSize,
            sizeof (sockBufferSize))) {
        nErrorCode = errno;
        CONSOLE_LOG("Failed to increase socket buffer size: %d %s", nErrorCode,
                strerror(nErrorCode));
        return nErrorCode;
    }
    // increase send buffer size
    if (0 != setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sockBufferSize,
            sizeof (sockBufferSize))) {
        nErrorCode = errno;
        CONSOLE_LOG("Failed to increase socket buffer size: %d %s", nErrorCode,
                strerror(nErrorCode));
        return nErrorCode;
    }

    return nErrorCode;
}

int bftps_socket_destroy(int* p_fd, bool session_socket) {
    // if pointer don't exist return invalid arguments
    if (!p_fd)
        return EINVAL;
    // if file descriptor is already -1, return success 
    if (0 > *p_fd)
        return 0;

    int nErrorCode = 0;
    // we only shutdown connection on sessions sockets fd not the listening ones
    if (session_socket) {
        // get peer address and print
        struct sockaddr_in addr;
        socklen_t addrlen = sizeof(addr);
        if (0 != getpeername(*p_fd, (struct sockaddr*) &addr, &addrlen)) {
            nErrorCode = errno;
            CONSOLE_LOG("getpeername: %d %s", nErrorCode, strerror(nErrorCode));
            CONSOLE_LOG("closing connection to fd=%d", *p_fd);
        } else
        {
            CONSOLE_LOG("closing connection to %s:%u",
                inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
        }
        
        // shutdown connection
        if (0 > shutdown(*p_fd, SHUT_WR)) {
            nErrorCode = errno;
            CONSOLE_LOG("Failed to shutdown session socket: %d %s", nErrorCode,
                    strerror(nErrorCode));
        } else {
            // wait for client to close connection
            struct pollfd fds[1];
            fds[0].fd = *p_fd;
            fds[0].events = POLLIN;
            fds[0].revents = 0;
            // try to wait for socket to shutdown before close it's handle
            if (0 > poll(fds, 1, 250)) {
                nErrorCode = errno;
                CONSOLE_LOG("Failed to poll session to destroy socket: %d %s", 
                        nErrorCode, strerror(nErrorCode));
            }
        }
    }

    // set linger to 0 to force connection to abort immediately
    struct linger linger;
    linger.l_onoff = 1;
    linger.l_linger = 0;

    if (0 != setsockopt(*p_fd, SOL_SOCKET, SO_LINGER,
            &linger, sizeof (linger))) {
        nErrorCode = errno;
        CONSOLE_LOG("setsockopt linger option: %d %s", nErrorCode, strerror(nErrorCode));
    }

    // close socket
    if (0 != close(*p_fd)) {
        nErrorCode = errno;
        CONSOLE_LOG("close socket: %d %s", nErrorCode, strerror(nErrorCode));
    }
    // change the original file descriptor to -1 so caller does not forget to do it
    *p_fd = -1;

    return nErrorCode;
}