#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <netinet/in.h>
#include <unistd.h>
#include <poll.h>
#include <string.h>
#include <arpa/inet.h>
#include <fcntl.h>

#include "bftps_session.h"
#include "bftps_command.h"
#include "bftps_socket.h"
#include "bool.h"

#define POLL_UNKNOWN (~(POLLIN|POLLPRI|POLLOUT))

extern void bftps_file_transfer_remove(bftps_session_context_t* session);

int bftps_session_init(bftps_session_context_t** p_session, int fd_listen) {
    // Check if pointer to receive the new session is valid, also validate
    // the file descriptor
    if (!p_session || *p_session || (0 > fd_listen))
        return EINVAL;

    bftps_session_context_t* session = malloc(sizeof(bftps_session_context_t));
    if(NULL == session)
        return ENOMEM;
    
    int nErrorCode = 0;

    // accept connection, saving the client connection address in the session
    // pasv address to be used later if needed
    int fdSession;
    socklen_t addressLenght = sizeof (session->pasvAddress);
    fdSession = accept(fd_listen, (struct sockaddr*) &session->pasvAddress,
            &addressLenght);
    if (0 > fdSession) {
        nErrorCode = errno;
        CONSOLE_LOG("Failed to accept new session: %d", nErrorCode);
    } else {
        // initialize session with default values
        strcpy(session->cwd, "/");
        session->commandFd = fdSession;
        session->commandBufferSize = 0;
        session->mode = BFTPS_SESSION_MODE_COMMAND;
        session->flags = 0;
        session->timestamp = 0;
        session->pasvFd = -1;
        session->dirMode = BFTPS_TRANSFER_DIR_MODE_INVALID;
        session->transfer = NULL;
        session->dataBufferPosition = 0;
        session->dataBufferSize = 0;
        session->dataFd = -1;
        session->dataAddress.sin_addr.s_addr = INADDR_ANY;
        session->dir = NULL;
        session->mlstFlags = BFTPS_TRANSFER_DIR_MLST_TYPE |
                BFTPS_TRANSFER_DIR_MLST_SIZE |
                BFTPS_TRANSFER_DIR_MLST_MODIFY |
                BFTPS_TRANSFER_DIR_MLST_PERM;
        //session->fileBig = false;
        //session->fileBigIO = NULL;
        session->filenameRefresh = false;
#ifdef _USE_FD_TRANSFER
        session->fileFd = -1;
#else
        session->filep = NULL;
#endif
        session->filepos = 0;
        session->filesize = 0;
        session->next = NULL;

        CONSOLE_LOG("Accepted connection from %s:%u",
                inet_ntoa(session->pasvAddress.sin_addr),
                ntohs(session->pasvAddress.sin_port));

        // send initiator response to client
        if (FAILED(nErrorCode = bftps_command_send_response(session, 220,
                "Hello!\r\n"))) {
            CONSOLE_LOG("Failed to send \"Hello!\": %d", nErrorCode);
        }
    }
    
    // Check if something has failed or not
    if(FAILED(nErrorCode))
        free(session);
    else
        *p_session = session;

    return nErrorCode;
}

int bftps_session_poll(bftps_session_context_t *session) {
    if (!session)
        return EINVAL;

    if (session->mode == BFTPS_SESSION_MODE_INVALID)
        return 0;

    if (session->commandFd == -1)
        return EINVAL;

    struct pollfd fds[2];

    // the first fd to poll is the command socket
    fds[0].fd = session->commandFd;
    fds[0].events = POLLIN | POLLPRI;
    fds[0].revents = 0;
    nfds_t nfds = 1;

    switch (session->mode) {
        case BFTPS_SESSION_MODE_COMMAND:
            // we are waiting to read a command
            break;
        case BFTPS_SESSION_MODE_DATA_CONNECT:
            if (session->flags & BFTPS_SESSION_FLAG_PASV) {
                // we are waiting for a PASV connection
                fds[1].fd = session->pasvFd;
                fds[1].events = POLLIN;
            } else {
                // we are waiting to complete a PORT connection
                fds[1].fd = session->dataFd;
                fds[1].events = POLLOUT;
            }
            fds[1].revents = 0;
            nfds = 2;
            break;
        case BFTPS_SESSION_MODE_DATA_TRANSFER:
            // we need to transfer data
            fds[1].fd = session->dataFd;
            if (session->flags & BFTPS_SESSION_FLAG_RECV)
                fds[1].events = POLLIN;
            else
                fds[1].events = POLLOUT;
            fds[1].revents = 0;
            nfds = 2;
            break;
        case BFTPS_SESSION_MODE_INVALID:
        default:
            CONSOLE_LOG("Invalid mode: %d", session->mode);
            break;
    }

    int nErrorCode = 0;

    // poll the selected sockets
    int result = poll(fds, nfds, 0);
    if (0 > result) {
        result = errno;
        CONSOLE_LOG("Failed to poll session socket: %d", result);
        return result;
    } else if (0 < result) {
        // check the command socket
        if (fds[0].revents != 0) {
            // handle received command 
            if (fds[0].revents & POLL_UNKNOWN)
            {
                CONSOLE_LOG("Unknown poll event received: %d", fds[0].revents);
            }

            // we need to read a new command
            if (fds[0].revents & (POLLERR | POLLHUP)) {
                CONSOLE_LOG("POLLERR|POLLHUP event received");
                return ECONNABORTED;
            } else if (fds[0].revents & (POLLIN | POLLPRI)) {
                if (FAILED(nErrorCode = bftps_command_receive(session,
                        fds[0].revents))) {
                    CONSOLE_LOG("Failed to receive the command: %d %s",
                            nErrorCode, strerror(nErrorCode));
                    return nErrorCode;
                }
            }
        }
    }

    /* check the data/pasv socket */
    if (nfds > 1 && fds[1].revents != 0) {
        switch (session->mode) {
            case BFTPS_SESSION_MODE_COMMAND:
                // this shouldn't happen?
                break;
            case BFTPS_SESSION_MODE_DATA_CONNECT:
                if (fds[1].revents & POLL_UNKNOWN)
                {
                    CONSOLE_LOG("pasvFd: revents=0x%08X", fds[1].revents);
                }

                // we need to accept the PASV connection
                if (fds[1].revents & (POLLERR | POLLHUP)) {
                    // errors occurred so let's revert to command mode
                    bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND,
                            BFTPS_SESSION_MODE_SET_FLAG_CLOSE_PASV |
                            BFTPS_SESSION_MODE_SET_FLAG_CLOSE_DATA);
                    bftps_command_send_response(session, 426, "Data connection failed\r\n");
                } else if (fds[1].revents & POLLIN) {
                    if (FAILED(bftps_session_accept(session)))
                        bftps_session_mode_set(session,
                            BFTPS_SESSION_MODE_COMMAND,
                            BFTPS_SESSION_MODE_SET_FLAG_CLOSE_PASV |
                            BFTPS_SESSION_MODE_SET_FLAG_CLOSE_DATA);
                } else if (fds[1].revents & POLLOUT) {
                    CONSOLE_LOG("connected to %s:%u",
                            inet_ntoa(session->dataAddress.sin_addr),
                            ntohs(session->dataAddress.sin_port));

                    bftps_session_mode_set(session, BFTPS_SESSION_MODE_DATA_TRANSFER,
                            BFTPS_SESSION_MODE_SET_FLAG_CLOSE_PASV);
                    bftps_command_send_response(session, 150, "Ready\r\n");
                }
                break;

            case BFTPS_SESSION_MODE_DATA_TRANSFER:
                if (fds[1].revents & POLL_UNKNOWN)
                {
                    CONSOLE_LOG("data_fd: revents=0x%08X", fds[1].revents);
                }

                // we need to transfer data
                if (fds[1].revents & (POLLERR | POLLHUP)) {
                    // errors occurred so let's revert to command mode
                    bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND,
                            BFTPS_SESSION_MODE_SET_FLAG_CLOSE_PASV |
                            BFTPS_SESSION_MODE_SET_FLAG_CLOSE_DATA);
                    bftps_command_send_response(session, 426, "Data connection failed\r\n");
                } else if (fds[1].revents & (POLLIN | POLLOUT))
                    bftps_session_transfer(session);
                break;
            case BFTPS_SESSION_MODE_INVALID:
            default:
                CONSOLE_LOG("Invalid mode: %d", session->mode);
                break;
        }
    }

    return 0;
}

// transfer loop
extern bool bftps_exiting();
int bftps_session_transfer(bftps_session_context_t *session) {
    int rc;
    do {
        rc = session->transfer(session);
    } while ((rc == BFTPS_TRANSFER_LOOP_STATUS_CONTINUE) 
            && (false == bftps_exiting()));
    
    return 0;
}

int bftps_session_destroy(bftps_session_context_t* session) {
    if (!session)
        return EINVAL;
    
    // Supposedly all connections where already closed when setting the mode
    // in bftps_session_mode_set, so let's just free the memory
    
    free(session);

    return 0;
}

// open current working directory for ftp session

int bftps_session_open_cwd(bftps_session_context_t *session) {
    int nErrorCode = 0;
    // open current working directory
    session->dir = opendir(session->cwd);
    if (session->dir == NULL) {
        nErrorCode = errno;
        CONSOLE_LOG("Failed to open dir [%s]: %d %s", session->cwd,
                nErrorCode, strerror(nErrorCode));
    }

    return nErrorCode;
}

// close current working directory for ftp session

int bftps_session_close_cwd(bftps_session_context_t *session) {
    // close open directory pointer
    int nErrorCode = 0;
    if (session->dir != NULL) {
        if (0 != closedir(session->dir)) {
            nErrorCode = errno;
            CONSOLE_LOG("Failed to close dir [%s]: %d %s", session->cwd,
                    nErrorCode, strerror(nErrorCode));
        }
    }
    session->dir = NULL;

    return nErrorCode;
}

int bftps_session_close_pasv(bftps_session_context_t *session) {
    if (!session)
        return EINVAL;
    if (0 > session->pasvFd)
        return 0;
    // close pasv socket
    CONSOLE_LOG("Stop listening on %s:%u",
            inet_ntoa(session->pasvAddress.sin_addr),
            ntohs(session->pasvAddress.sin_port));
    return bftps_socket_destroy(&session->pasvFd, false);
}

// close command socket on ftp session

int bftps_session_close_cmd(bftps_session_context_t *session) {
    // close command socket
    if (0 <= session->commandFd)
        return bftps_socket_destroy(&session->commandFd, true);
    else
        return 0;
}

// close data socket on ftp session

int bftps_session_close_data(bftps_session_context_t *session) {
    // close data connection
    if (session->dataFd >= 0 && session->dataFd != session->commandFd)
        bftps_socket_destroy(&session->dataFd, true);

    // clear send/recv flags
    session->flags &= ~(BFTPS_SESSION_FLAG_RECV | BFTPS_SESSION_FLAG_SEND);

    return 0;
}

// close open file for ftp session

int bftps_session_close_file(bftps_session_context_t *session) {
    // we should remove this transfer information
    bftps_file_transfer_remove(session);
    
    int nErrorCode = 0;
#ifdef _USE_FD_TRANSFER
    if (-1 != session->fileFd) {
        if (-1 == close(session->fileFd)) {
            nErrorCode = errno;
            CONSOLE_LOG("close: %d %s", nErrorCode, strerror(nErrorCode));
        }
    }
    session->fileFd = -1;
#else
    if (NULL != session->filep) {
        if (0 != fclose(session->filep)) {
            nErrorCode = errno;
            CONSOLE_LOG("fclose: %d %s", nErrorCode, strerror(nErrorCode));
        }
    }
    session->filep = NULL;
#endif

    /* if(NULL != session->fileBigIO)
         file_io_destroy(&session->fileBigIO);*/

    session->filepos = 0;

    return nErrorCode;
}

int bftps_session_mode_set(bftps_session_context_t *session,
        bftps_session_mode_t mode, bftps_session_mode_set_flags_t flags) {
    if (!session)
        return EINVAL;

    session->mode = mode;

    // close everything if it is to destroy
    if (session->mode == BFTPS_SESSION_MODE_DESTROY) {
        bftps_session_close_pasv(session);
        bftps_session_close_data(session);
        bftps_session_close_file(session);
        bftps_session_close_cwd(session);
        bftps_session_close_cmd(session);
    } else {
        // close pasv and data sockets
        if (flags & BFTPS_SESSION_MODE_SET_FLAG_CLOSE_PASV)
            bftps_session_close_pasv(session);
        if (flags & BFTPS_SESSION_MODE_SET_FLAG_CLOSE_DATA)
            bftps_session_close_data(session);

        if (session->mode == BFTPS_SESSION_MODE_COMMAND) {
            // close file/cwd 
            bftps_session_close_file(session);
            bftps_session_close_cwd(session);
        }
    }

    return 0;
}


// set a socket to non-blocking

int bftps_session_set_socket_nonblocking(int fd) {
    int nErrorCode = 0;
    //get the socket flags 
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        nErrorCode = errno;
        CONSOLE_LOG("fcntl: %d %s", nErrorCode, strerror(nErrorCode));
        return nErrorCode;
    }

    // add O_NONBLOCK to the socket flags
    if (0 != fcntl(fd, F_SETFL, flags | O_NONBLOCK)) {
        nErrorCode = errno;
        CONSOLE_LOG("fcntl: %d %s", nErrorCode, strerror(nErrorCode));
        return nErrorCode;
    }

    return nErrorCode;
}

// accept PASV connection for ftp session

int bftps_session_accept(bftps_session_context_t *session) {

    if (session->flags & BFTPS_SESSION_FLAG_PASV) {
        // clear PASV flag
        session->flags &= ~BFTPS_SESSION_FLAG_PASV;

        // tell the peer that we're ready to accept the connection
        bftps_command_send_response(session, 150, "Ready\r\n");

        int nErrorCode = 0;
        // accept connection from peer
        struct sockaddr_in addr;
        socklen_t addrlen = sizeof (addr);
        int newFd = accept(session->pasvFd, (struct sockaddr*) &addr, &addrlen);
        if (0 > newFd) {
            nErrorCode = errno;
            CONSOLE_LOG("accept: %d %s", nErrorCode, strerror(nErrorCode));
            bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND,
                    BFTPS_SESSION_MODE_SET_FLAG_CLOSE_PASV |
                    BFTPS_SESSION_MODE_SET_FLAG_CLOSE_DATA);
            bftps_command_send_response(session, 425, "Failed to establish connection\r\n");
            return nErrorCode;
        }

        // set the socket to non-blocking
        if (FAILED(nErrorCode = bftps_session_set_socket_nonblocking(newFd))) {
            bftps_socket_destroy(&newFd, true);
            bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND,
                    BFTPS_SESSION_MODE_SET_FLAG_CLOSE_PASV |
                    BFTPS_SESSION_MODE_SET_FLAG_CLOSE_DATA);
            bftps_command_send_response(session, 425, "Failed to establish connection\r\n");
            return -1;
        }

        CONSOLE_LOG("accepted connection from %s:%u",
                inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));

        // we are ready to transfer data
        bftps_session_mode_set(session, BFTPS_SESSION_MODE_DATA_TRANSFER,
                BFTPS_SESSION_MODE_SET_FLAG_CLOSE_PASV);
        session->dataFd = newFd;

        return 0;
    } else {
        // peer didn't send PASV command 
        bftps_command_send_response(session, 503, "Bad sequence of commands\r\n");
        return -1;
    }
}

// connect to peer for ftp session

int bftps_session_connect(bftps_session_context_t *session) {
    int nErrorCode = 0;
    // clear PORT flag
    session->flags &= ~BFTPS_SESSION_FLAG_PORT;

    // create a new socket
    session->dataFd = socket(AF_INET, SOCK_STREAM, 0);
    if (0 > session->dataFd) {
        nErrorCode = errno;
        CONSOLE_LOG("Failed to create socket: %d %s",
                nErrorCode, strerror(nErrorCode));
        return nErrorCode;
    }

    // set socket options
    if (FAILED(nErrorCode = bftps_socket_options_increase_buffers(session->dataFd))) {
        bftps_socket_destroy(&session->dataFd, false);
        return nErrorCode;
    }

    // set the socket to non-blocking
    if (FAILED(nErrorCode = bftps_session_set_socket_nonblocking(session->dataFd))) {
        return -1;
    }
  
    // connect to peer
    if (0 != connect(session->dataFd, (struct sockaddr*) &session->dataAddress,
            sizeof (session->dataAddress))) {
        if (errno != EINPROGRESS) {
            nErrorCode = errno;
            CONSOLE_LOG("Failed to connect: %d %s",
                    nErrorCode, strerror(nErrorCode));
            bftps_socket_destroy(&session->dataFd, false);
            return nErrorCode;
        }
    } else {
        CONSOLE_LOG("connected to %s:%u",
                inet_ntoa(session->dataAddress.sin_addr),
                ntohs(session->dataAddress.sin_port));

        bftps_session_mode_set(session, BFTPS_SESSION_MODE_DATA_TRANSFER, BFTPS_SESSION_MODE_SET_FLAG_CLOSE_PASV);
        bftps_command_send_response(session, 150, "Ready\r\n");
    }

    return nErrorCode;
}

