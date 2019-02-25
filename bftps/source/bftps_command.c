#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <poll.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <inttypes.h>
#ifdef _3DS
#include <3ds.h>
#endif

#include "bftps_command.h"
#include "bftps_socket.h"
#include "bftps_common.h"
#include "bftps_transfer_dir.h"
#include "bftps_transfer_file.h"

#include "macros.h"
#include "bool.h"


#define BFTPS_SESSION_COMMAND_BUFFERSIZE 1024

extern time_t bftps_start_time();

#define FTP_DECLARE(x) int x(bftps_session_context_t *session, const char *args)

FTP_DECLARE(ABOR);
FTP_DECLARE(ALLO);
FTP_DECLARE(APPE);
FTP_DECLARE(CDUP);
FTP_DECLARE(CWD);
FTP_DECLARE(DELE);
FTP_DECLARE(FEAT);
FTP_DECLARE(HELP);
FTP_DECLARE(LIST);
FTP_DECLARE(MDTM);
FTP_DECLARE(MKD);
FTP_DECLARE(MLSD);
FTP_DECLARE(MLST);
FTP_DECLARE(MODE);
FTP_DECLARE(NLST);
FTP_DECLARE(NOOP);
FTP_DECLARE(OPTS);
FTP_DECLARE(PASS);
FTP_DECLARE(PASV);
FTP_DECLARE(PORT);
FTP_DECLARE(PWD);
FTP_DECLARE(QUIT);
FTP_DECLARE(REST);
FTP_DECLARE(RETR);
FTP_DECLARE(RMD);
FTP_DECLARE(RNFR);
FTP_DECLARE(RNTO);
FTP_DECLARE(SIZE);
FTP_DECLARE(STAT);
FTP_DECLARE(STOR);
FTP_DECLARE(STOU);
FTP_DECLARE(STRU);
FTP_DECLARE(SYST);
FTP_DECLARE(TYPE);
FTP_DECLARE(USER);


// ftp command descriptor

typedef struct {
    const char *name; // command name
    int (*handler)(bftps_session_context_t*, const char*); // command callback
} bftps_command_t;

// ftp command list
static bftps_command_t bftps_commands[] = {
    // ftp command
#define FTP_COMMAND(x) { #x, x, }
    // ftp alias
#define FTP_ALIAS(x,y) { #x, y, }
    
    FTP_COMMAND(ABOR),
    FTP_COMMAND(ALLO),
    FTP_COMMAND(APPE),
    FTP_COMMAND(CDUP),
    FTP_COMMAND(CWD),
    FTP_COMMAND(DELE),
    FTP_COMMAND(FEAT),
    FTP_COMMAND(HELP),
    FTP_COMMAND(LIST),
    FTP_COMMAND(MDTM),
    FTP_COMMAND(MKD),
    FTP_COMMAND(MLSD),
    FTP_COMMAND(MLST),
    FTP_COMMAND(MODE),
    FTP_COMMAND(NLST),
    FTP_COMMAND(NOOP),
    FTP_COMMAND(OPTS),
    FTP_COMMAND(PASS),
    FTP_COMMAND(PASV),
    FTP_COMMAND(PORT),
    FTP_COMMAND(PWD),
    FTP_COMMAND(QUIT),
    FTP_COMMAND(REST),
    FTP_COMMAND(RETR),
    FTP_COMMAND(RMD),
    FTP_COMMAND(RNFR),
    FTP_COMMAND(RNTO),
    FTP_COMMAND(SIZE),
    FTP_COMMAND(STAT),
    FTP_COMMAND(STOR),
    FTP_COMMAND(STOU),
    FTP_COMMAND(STRU),
    FTP_COMMAND(SYST),
    FTP_COMMAND(TYPE),
    FTP_COMMAND(USER),
    FTP_ALIAS(XCUP, CDUP),
    FTP_ALIAS(XCWD, CWD),
    FTP_ALIAS(XMKD, MKD),
    FTP_ALIAS(XPWD, PWD),
    FTP_ALIAS(XRMD, RMD),
};
// number of ftp commands
static const size_t bftps_commands_total =
        sizeof (bftps_commands) / sizeof (bftps_commands[0]);

int bftps_command_cmp(const void *p1, const void *p2) {
    bftps_command_t *c1 = (bftps_command_t*) p1;
    bftps_command_t *c2 = (bftps_command_t*) p2;

    // ordered by command name
    return strcasecmp(c1->name, c2->name);
}

#ifdef _3DS
//TODO find better way to do this
#define DATA_PORT       (5000+1) /* BFTPS_PORT_LISTEN*/ 
#else
#define DATA_PORT       0 /* ephemeral port */
#endif

in_port_t bftps_command_next_data_port(void) {
#ifdef _3DS
    static in_port_t dataPort = DATA_PORT; 
    if (++dataPort >= 10000)
        dataPort = DATA_PORT;
    return dataPort;
#else
    return 0; /* ephemeral port */
#endif
}

#ifdef __GNUC__
__attribute__ ((format(printf, 3, 4)))
#endif
int bftps_command_send_response(bftps_session_context_t *session,
        int code, const char *fmt, ...) {

    if (0 >= session->commandFd)
        return EINVAL;

    static char buffer[BFTPS_SESSION_COMMAND_BUFFERSIZE];

    // print response code and message to buffer
    size_t length;
    va_list va;
    va_start(va, fmt);
    if (code > 0)
        length = sprintf(buffer, "%d ", code);
    else
        length = sprintf(buffer, "%d-", -code);
    length += vsnprintf(buffer + length, sizeof (buffer) - length, fmt, va);
    va_end(va);

    return bftps_command_send_response_buffer(session, buffer, length);
}

int bftps_command_send_response_buffer(bftps_session_context_t *session,
        const char * buffer, ssize_t length) {

    if (0 >= session->commandFd)
        return EINVAL;

    int nErrorCode = 0;

    // send command
    CONSOLE_LOG_INLINE("%s", buffer);
    ssize_t result = send(session->commandFd, buffer, length, MSG_NOSIGNAL);
    if (0 > result) {
        nErrorCode = errno;
        CONSOLE_LOG("Failed to send command [%s]: %d %s", buffer, nErrorCode, strerror(nErrorCode));
    } else if (result != length) {
        CONSOLE_LOG("Sent only some bytes [%d/%d]", (int) result, (int) length);
        //TODO: check if we can sent the remaining later
        nErrorCode = ECONNABORTED;
    }

    return nErrorCode;
}

int bftps_command_receive(bftps_session_context_t *session, int events) {
    int nErrorCode = 0;
    // check out-of-band data
    if (events & POLLPRI) {
        session->flags |= BFTPS_SESSION_FLAG_URGENT;

        // check if we are at the urgent marker
        int atMark = sockatmark(session->commandFd);
        if (atMark < 0) {
            nErrorCode = errno;
            CONSOLE_LOG("Failed to call sockatmark for the session fd: %d %s",
                    nErrorCode, strerror(nErrorCode));
            return nErrorCode;
        }

        if (!atMark) {
            // discard in-band data
            if (0 > recv(session->commandFd, session->commandBuffer, sizeof (session->commandBuffer), 0)) {
                nErrorCode = errno;
                CONSOLE_LOG("Failed to discard in-band data: %d %s",
                        nErrorCode, strerror(nErrorCode));
                return nErrorCode;
            } else // check again
            {
                // check if we are at the urgent marker
                int atMark = sockatmark(session->commandFd);
                if (atMark < 0) {
                    nErrorCode = errno;
                    CONSOLE_LOG("Failed to call sockatmark for the session fd: %d %s",
                            nErrorCode, strerror(nErrorCode));
                    return nErrorCode;
                }
                if (!atMark)
                    return 0; // next time we will read more data to discard
            }
        }

        // retrieve the urgent data
        if (0 > recv(session->commandFd, session->commandBuffer, sizeof (session->commandBuffer),
                MSG_OOB)) {
            nErrorCode = errno;
            CONSOLE_LOG("Failed to retrieve the urgent data: %d %s", nErrorCode, strerror(nErrorCode));
            return nErrorCode;
        }

        // reset the command buffer since we use it to discard the data
        session->commandBufferSize = 0;
        return 0;
    }

    // prepare to receive data
    char* buffer = session->commandBuffer + session->commandBufferSize;
    size_t len = sizeof (session->commandBuffer) - session->commandBufferSize;
    if (len == 0) {
        // error retrieving command
        nErrorCode = EOVERFLOW;
        CONSOLE_LOG("Exceeded command buffer size: %d %s", nErrorCode, strerror(nErrorCode));
        return nErrorCode;
    }

    // retrieve command data
    int result = recv(session->commandFd, buffer, len, 0);
    if (0 > result) {
        // error retrieving command
        nErrorCode = errno;
        CONSOLE_LOG("Failed to receive the command: %d %s", nErrorCode, strerror(nErrorCode));
        return nErrorCode;
    }
    if (0 == result) {
        // peer closed connection
        nErrorCode = ECONNABORTED;
        CONSOLE_LOG("Peer closed connection: %d %s", nErrorCode, strerror(nErrorCode));
        return nErrorCode;
    } else {
        session->commandBufferSize += result;
        len = sizeof (session->commandBuffer) - session->commandBufferSize;

        if (session->flags & BFTPS_SESSION_FLAG_URGENT) {
            // look for telnet data mark
            size_t i = 0;
            for (i = 0; i < session->commandBufferSize; ++i) {
                if ((unsigned char) session->commandBuffer[i] == 0xF2) {
                    // ignore all data that precedes the data mark
                    if (i < session->commandBufferSize - 1)
                        memmove(session->commandBuffer, session->commandBuffer + i + 1, len - i - 1);
                    session->commandBufferSize -= i + 1;
                    session->flags &= ~BFTPS_SESSION_FLAG_URGENT;
                    break;
                }
            }
        }

        char* next = NULL;
        // loop through commands
        while (true) {
            // must have at least enough data for the delimiter
            if (session->commandBufferSize < 1)
                return 0;

            // look for \r\n or \n delimiter
            size_t i = 0;
            for (i = 0; i < session->commandBufferSize; ++i) {
                if (i < session->commandBufferSize - 1
                        && session->commandBuffer[i] == '\r'
                        && session->commandBuffer[i + 1] == '\n') {
                    // we found a \r\n delimiter
                    session->commandBuffer[i] = '\0';
                    next = &session->commandBuffer[i + 2];
                    break;
                } else if (session->commandBuffer[i] == '\n') {
                    // we found a \n delimiter
                    session->commandBuffer[i] = '\0';
                    next = &session->commandBuffer[i + 1];
                    break;
                }
            }
            // check if a delimiter was found
            if (i == session->commandBufferSize)
                return 0;

            // decode the command
            bftps_common_decode_buffer(session->commandBuffer, i);

            // split command from arguments
            char* args = buffer = session->commandBuffer;
            while (*args && !isspace((int) *args))
                ++args;
            if (*args)
                *args++ = '\0';

            // look up the command
            bftps_command_t key = {buffer, NULL};
            bftps_command_t* command = bsearch(&key, bftps_commands,
                    bftps_commands_total,
                    sizeof (bftps_command_t), bftps_command_cmp);

            // update command timestamp
            session->timestamp = time(NULL);

            // execute the command
            if (command == NULL) {  
                if(*args)
                {
                    CONSOLE_LOG("Unimplemented command: %s %s", key.name, args);
                }
                else
                {
                    CONSOLE_LOG("Unimplemented command: %s", key.name);
                }
                // send header
                if (FAILED(nErrorCode = bftps_command_send_response(
                        session, 502, "Invalid command \""))) {
                    CONSOLE_LOG("Failed to reply to client: %d", nErrorCode);
                    return nErrorCode;
                }
                // send command
                len = strlen(buffer);
                buffer = bftps_common_encode_buffer(buffer, &len, false);
                if (buffer != NULL) {
                    if (FAILED(nErrorCode =
                            bftps_command_send_response_buffer(session,
                            buffer, len))) {
                        CONSOLE_LOG("Failed to reply to client: %d", nErrorCode);
                        free(buffer);
                        return nErrorCode;
                    } else
                        free(buffer);
                } else {
                    if (FAILED(nErrorCode =
                            bftps_command_send_response_buffer(session,
                            key.name, strlen(key.name)))) {
                        CONSOLE_LOG("Failed to reply to client: %d", nErrorCode);
                        return nErrorCode;
                    }
                }

                // send args (if any)
                if (*args != 0) {
                    if (FAILED(nErrorCode =
                            bftps_command_send_response_buffer(session,
                            " ", 1))) {
                        CONSOLE_LOG("Failed to reply to client: %d", nErrorCode);
                        return nErrorCode;
                    }

                    len = strlen(args);
                    buffer = bftps_common_encode_buffer(args, &len, false);
                    if (buffer != NULL) {
                        if (FAILED(nErrorCode =
                                bftps_command_send_response_buffer(
                                session, buffer, len))) {
                            CONSOLE_LOG("Failed to reply to client: %d", nErrorCode);
                            free(buffer);
                            return nErrorCode;
                        } else
                            free(buffer);
                    } else {
                        if (FAILED(nErrorCode =
                                bftps_command_send_response_buffer(
                                session, args, strlen(args)))) {
                            CONSOLE_LOG("Failed to reply to client: %d", nErrorCode);
                            return nErrorCode;
                        }
                    }
                }

                // send footer
                if (FAILED(nErrorCode =
                        bftps_command_send_response_buffer(session,
                        "\"\r\n", 3))) {
                    CONSOLE_LOG("Failed to reply to client: %d", nErrorCode);
                    return nErrorCode;
                }
            } else if (session->mode != BFTPS_SESSION_MODE_COMMAND) {
                // only some commands are available during data transfer
                if (strcasecmp(command->name, "ABOR") != 0
                        && strcasecmp(command->name, "STAT") != 0
                        && strcasecmp(command->name, "QUIT") != 0) {
                    bftps_command_send_response(session, 503,
                            "Invalid command during transfer\r\n");
                    bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND,
                            BFTPS_SESSION_MODE_SET_FLAG_CLOSE_PASV |
                            BFTPS_SESSION_MODE_SET_FLAG_CLOSE_PASV);
                    bftps_session_close_cmd(session);
                } else
                    command->handler(session, args);
            } else {
                // clear RENAME flag for all commands except RNTO
                if (strcasecmp(command->name, "RNTO") != 0)
                    session->flags &= ~BFTPS_SESSION_FLAG_RENAME;

                if (FAILED(nErrorCode = command->handler(session, args))) {
                    CONSOLE_LOG("Failed to handle command: %d", nErrorCode);
                    return nErrorCode;
                }
            }

            // remove executed command from the command buffer
            len = session->commandBuffer + session->commandBufferSize - next;
            if (len > 0)
                memmove(session->commandBuffer, next, len);
            session->commandBufferSize = len;
        }
    }

    return 0;
}

// abort a transfer

FTP_DECLARE(ABOR) {
    CONSOLE_LOG("ABOR %s", args ? args : "");

    if (session->mode == BFTPS_SESSION_MODE_COMMAND) {
        return bftps_command_send_response(session, 225, "No transfer to abort\r\n");
    }

    // abort the transfer
    bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND,
            BFTPS_SESSION_MODE_SET_FLAG_CLOSE_PASV |
            BFTPS_SESSION_MODE_SET_FLAG_CLOSE_DATA);

    // send response for this request
    bftps_command_send_response(session, 225, "Aborted\r\n");

    // send response for transfer 
    return bftps_command_send_response(session, 425, "Transfer aborted\r\n");
}

// allocate space

FTP_DECLARE(ALLO) {
    CONSOLE_LOG("ALLO %s", args ? args : "");

    bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND, 0);

    return bftps_command_send_response(session, 202, "superfluous command\r\n");
}

// append data to a file - requires a PASV or PORT connection

FTP_DECLARE(APPE) {
    CONSOLE_LOG("APPE %s", args ? args : "");

    // open the file in append mode 
    return bftps_transfer_file(session, args, BFTPS_TRANSFER_FILE_APPE);
}

// CWD to parent directory

FTP_DECLARE(CDUP) {
    CONSOLE_LOG("CDUP %s", args ? args : "");

    bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND, 0);

    // change to parent directory
    bftps_common_cd_up(session);

    return bftps_command_send_response(session, 200, "OK\r\n");
}

// change working directory
FTP_DECLARE(CWD) {
    CONSOLE_LOG("CWD %s", args ? args : "");

    bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND, 0);

    // is equivalent to CDUP
    if (strcmp(args, "..") == 0) {
        bftps_common_cd_up(session);        
        return bftps_command_send_response(session, 200, "OK\r\n");
    }

    int nErrorCode = 0;
    // build the new cwd path
    if (FAILED(nErrorCode = bftps_common_build_path(session, session->cwd, args))) {        
        return bftps_command_send_response(session, 553, "%s\r\n", strerror(nErrorCode));
    }

    // get the path status
    struct stat st;
    if (0 != stat(session->dataBuffer, &st)) {
        nErrorCode = errno;
        CONSOLE_LOG("stat '%s': %d %s", session->dataBuffer, nErrorCode, strerror(nErrorCode));        
        return bftps_command_send_response(session, 550, "unavailable\r\n");
    }

    // make sure it is a directory
    if (!S_ISDIR(st.st_mode)) {        
        return bftps_command_send_response(session, 553, "not a directory\r\n");
    }

    // copy the path into the cwd
    strncpy(session->cwd, session->dataBuffer, sizeof (session->cwd));
    session->cwd[sizeof (session->cwd) - 1] = '\0';
    return bftps_command_send_response(session, 200, "OK\r\n");
}

// delete a file

FTP_DECLARE(DELE) {
    CONSOLE_LOG("DELE %s", args ? args : "");

    bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND, 0);

    // build the file path
    int nErrorCode = 0;
    if (FAILED(nErrorCode = bftps_common_build_path(session, session->cwd, args))) {
        return bftps_command_send_response(session, 553, "%s\r\n", strerror(nErrorCode));
    }

    // try to unlink the path
    if (0 != unlink(session->dataBuffer)) {
        // error unlinking the file
        nErrorCode = errno;
        CONSOLE_LOG("unlink: %d %s\n", nErrorCode, strerror(nErrorCode));
        return bftps_command_send_response(session, 550, "failed to delete file\r\n");
    }

    bftps_common_update_free_space(session);
    return bftps_command_send_response(session, 250, "OK\r\n");
}

// list server features

FTP_DECLARE(FEAT) {
    CONSOLE_LOG("FEAT %s", args ? args : "");

    bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND, 0);

    // list our features
    return bftps_command_send_response(session, -211, "\r\n"
            " MDTM\r\n"
            " MLST Type%s;Size%s;Modify%s;Perm%s;UNIX.mode%s;\r\n"
            " PASV\r\n"
            " SIZE\r\n"
            " TVFS\r\n"
            " UTF8\r\n"
            "\r\n"
            "211 End\r\n",
            session->mlstFlags & BFTPS_TRANSFER_DIR_MLST_TYPE ? "*" : "",
            session->mlstFlags & BFTPS_TRANSFER_DIR_MLST_SIZE ? "*" : "",
            session->mlstFlags & BFTPS_TRANSFER_DIR_MLST_MODIFY ? "*" : "",
            session->mlstFlags & BFTPS_TRANSFER_DIR_MLST_PERM ? "*" : "",
            session->mlstFlags & BFTPS_TRANSFER_DIR_MLST_UNIX_MODE ? "*" : "");
}

// print server help

FTP_DECLARE(HELP) {
    CONSOLE_LOG("HELP %s", args ? args : "");

    bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND, 0);

    // list our accepted commands
    return bftps_command_send_response(session, -214,
            "The following commands are recognized\r\n"
            " ABOR ALLO APPE CDUP CWD DELE FEAT HELP LIST MDTM MKD MLSD MLST MODE\r\n"
            " NLST NOOP OPTS PASS PASV PORT PWD QUIT REST RETR RMD RNFR RNTO STAT\r\n"
            " STOR STOU STRU SYST TYPE USER XCUP XCWD XMKD XPWD XRMD\r\n"
            "214 End\r\n");
}

// retrieve a directory listing - Requires a PORT or PASV connection

FTP_DECLARE(LIST) {
    CONSOLE_LOG("LIST %s", args ? args : "");

    // open the path in LIST mode
    return bftps_transfer_dir(session, args, BFTPS_TRANSFER_DIR_MODE_LIST, true);
}

// get last modification time

FTP_DECLARE(MDTM) {
    CONSOLE_LOG("MDTM %s", args ? args : "");    

    bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND, 0);

    // build the path
    int nErrorCode = 0;
    if (FAILED(nErrorCode = bftps_common_build_path(session, session->cwd, args))) {
        return bftps_command_send_response(session, 553, "%s\r\n", strerror(nErrorCode));
    }

    time_t t_mtime;
#ifdef _3DS
    uint64_t mtime;
    if ( R_FAILED(sdmc_getmtime(session->dataBuffer, &mtime))) {
        return bftps_command_send_response(session, 550, "Error getting mtime\r\n");
    }
    t_mtime = mtime;
#else
    struct stat st;
    if (0 != stat(session->dataBuffer, &st)) {
        return bftps_command_send_response(session, 550, "Error getting mtime\r\n");
    }
    t_mtime = st.st_mtime;
#endif

    struct tm *tm = gmtime(&t_mtime);
    if (tm == NULL) {
        return bftps_command_send_response(session, 550, "Error getting mtime\r\n");
    }

    session->dataBufferSize = strftime(session->dataBuffer, sizeof (session->dataBuffer), "%Y%m%d%H%M%S", tm);
    if (session->dataBufferSize == 0) {
        return bftps_command_send_response(session, 550, "Error getting mtime\r\n");
    }

    session->dataBuffer[session->dataBufferSize] = '\0';

    return bftps_command_send_response(session, 213, "%s\r\n", session->dataBuffer);
}

// create a directory

FTP_DECLARE(MKD) {
    CONSOLE_LOG("MKD %s", args ? args : "");

    bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND, 0);

    // build the path
    int nErrorCode = 0;
    if (FAILED(nErrorCode = bftps_common_build_path(session, session->cwd, args))) {
        return bftps_command_send_response(session, 553, "%s\r\n", strerror(nErrorCode));
    }

    // try to create the directory
    if (0 != mkdir(session->dataBuffer, 0755)) {
        // mkdir failure
        int nErrorCode = errno;
        if(nErrorCode == EEXIST)
            return bftps_command_send_response(session, 553, "%s\r\n", strerror(errno));
        CONSOLE_LOG("mkdir: %d %s", nErrorCode, strerror(nErrorCode));        
        return bftps_command_send_response(session, 550, "failed to create directory\r\n");
    }

    bftps_common_update_free_space(session);
    return bftps_command_send_response(session, 250, "OK\r\n");
}

// retrieve machine list details for all files in current dir or argument

FTP_DECLARE(MLSD) {
    CONSOLE_LOG("MLSD %s", args ? args : "");

    // open the path in MLSD mode
    return bftps_transfer_dir(session, args, BFTPS_TRANSFER_DIR_MODE_MLSD, true);
}

// retrieve machine list details for current dir or argument

FTP_DECLARE(MLST) {
    CONSOLE_LOG("MLST %s", args ? args : "");

    bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND, 0);

    // build the path
    int nErrorCode = 0;
    if (FAILED(nErrorCode = bftps_common_build_path(session, session->cwd, args))) {
        return bftps_command_send_response(session, 501, "%s\r\n", strerror(nErrorCode));
    }

    // stat path
    struct stat st;
    if (0 != lstat(session->dataBuffer, &st)) {
        return bftps_command_send_response(session, 550, "%s\r\n", strerror(errno));
    }

    // encode \n in path
    size_t len = session->dataBufferSize;
    char * path = bftps_common_encode_buffer(session->dataBuffer, &len, true);
    if (!path) {
        return bftps_command_send_response(session, 550, "%s\r\n", strerror(ENOMEM));
    }

    session->dirMode = BFTPS_TRANSFER_DIR_MODE_MLST;
    nErrorCode = bftps_transfer_dir_fill_dirent(session, &st, path, len);
    free(path);
    if (FAILED(nErrorCode)) {
        return bftps_command_send_response(session, 550, "%s\r\n", strerror(nErrorCode));
    }

    path = malloc(session->dataBufferSize + 1);
    if (!path) {
        return bftps_command_send_response(session, 550, "%s\r\n", strerror(ENOMEM));
    }

    memcpy(path, session->dataBuffer, session->dataBufferSize);
    path[session->dataBufferSize] = '\0';
    nErrorCode = bftps_command_send_response(session, -250, "Status\r\n%s250 End\r\n", path);
    free(path);
    return nErrorCode;
}

// set transfer mode

FTP_DECLARE(MODE) {
    CONSOLE_LOG("MODE %s", args ? args : "");

    bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND, 0);

    // we only accept S (stream) mode
    if (strcasecmp(args, "S") == 0) {
        return bftps_command_send_response(session, 200, "OK\r\n");
    }

    return bftps_command_send_response(session, 504, "unavailable\r\n");
}

// retrieve a name list - Requires a PASV or PORT connection

FTP_DECLARE(NLST) {
    CONSOLE_LOG("NLST %s", args ? args : "");

    // open the path in NLST mode
    return bftps_transfer_dir(session, args, BFTPS_TRANSFER_DIR_MODE_NLST, false);
}

// no-op

FTP_DECLARE(NOOP) {
    CONSOLE_LOG("NOOP %s", args ? args : "");

    // this is a no-op
    return bftps_command_send_response(session, 200, "OK\r\n");
}

// set options - mostly for MLST information retrieval
FTP_DECLARE(OPTS) {
    CONSOLE_LOG("OPTS %s", args ? args : "");

    bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND, 0);

    // we accept the following UTF8 options
    if (strcasecmp(args, "UTF8") == 0
            || strcasecmp(args, "UTF8 ON") == 0
            || strcasecmp(args, "UTF8 NLST") == 0) {
        return bftps_command_send_response(session, 200, "OK\r\n");
    }

    // check MLST options
    if (strncasecmp(args, "MLST ", 5) == 0) {

        static const struct {
            const char *name;
            bftps_transfer_dir_mlst_flags_t flag;
        } mlst_flags[] ={
            { "Type;", BFTPS_TRANSFER_DIR_MLST_TYPE,},
            { "Size;", BFTPS_TRANSFER_DIR_MLST_SIZE,},
            { "Modify;", BFTPS_TRANSFER_DIR_MLST_MODIFY,},
            { "Perm;", BFTPS_TRANSFER_DIR_MLST_PERM,},
            { "UNIX.mode;", BFTPS_TRANSFER_DIR_MLST_UNIX_MODE,},
        };
        static const size_t num_mlst_flags = sizeof (mlst_flags) / sizeof (mlst_flags[0]);

        bftps_transfer_dir_mlst_flags_t flags = 0;
        args += 5;
        const char *p = args;
        while (*p) {
            for (size_t i = 0; i < num_mlst_flags; ++i) {
                if (strncasecmp(mlst_flags[i].name, p, strlen(mlst_flags[i].name)) == 0) {
                    flags |= mlst_flags[i].flag;
                    p += strlen(mlst_flags[i].name) - 1;
                    break;
                }
            }

            while (*p && *p != ';')
                ++p;

            if (*p == ';')
                ++p;
        }

        session->mlstFlags = flags;
        return bftps_command_send_response(session, 200, "MLST OPTS%s%s%s%s%s%s\r\n",
                flags ? " " : "",
                flags & BFTPS_TRANSFER_DIR_MLST_TYPE ? "Type;" : "",
                flags & BFTPS_TRANSFER_DIR_MLST_SIZE ? "Size;" : "",
                flags & BFTPS_TRANSFER_DIR_MLST_MODIFY ? "Modify;" : "",
                flags & BFTPS_TRANSFER_DIR_MLST_PERM ? "Perm;" : "",
                flags & BFTPS_TRANSFER_DIR_MLST_UNIX_MODE ? "UNIX.mode;" : "");
    }

    return bftps_command_send_response(session, 504, "invalid argument\r\n");
}

// provide password

FTP_DECLARE(PASS) {
    CONSOLE_LOG("PASS %s", args ? args : "");

    // we accept any password
    bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND, 0);

    return bftps_command_send_response(session, 230, "OK\r\n");
}

// request an address to connect to

FTP_DECLARE(PASV) {
    CONSOLE_LOG("PASV %s", args ? args : "");
    bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND,
            BFTPS_SESSION_MODE_SET_FLAG_CLOSE_DATA |
            BFTPS_SESSION_MODE_SET_FLAG_CLOSE_PASV);
    session->flags &= ~(BFTPS_SESSION_FLAG_PASV | BFTPS_SESSION_FLAG_PORT);

    int nErrorCode = 0;
    // create a socket to listen on
    session->pasvFd = socket(AF_INET, SOCK_STREAM, 0);
    if (session->pasvFd < 0) {
        nErrorCode = errno;
        CONSOLE_LOG("Failed to create socket for PASV: %d %s", nErrorCode, strerror(nErrorCode))
        return bftps_command_send_response(session, 451, "\r\n");
    }
    
    // set the socket options
    if (FAILED(nErrorCode = bftps_socket_options_increase_buffers(session->pasvFd))) {
        // failed to set socket options
        CONSOLE_LOG("Failed to increase the buffer size: %d %s", nErrorCode,
                strerror(nErrorCode));
        bftps_session_close_pasv(session);
        return bftps_command_send_response(session, 451, "\r\n");
    }
    // grab a new port
    session->pasvAddress.sin_port = htons(bftps_command_next_data_port());

    // bind to the port
    if (0 != bind(session->pasvFd, (struct sockaddr*) &session->pasvAddress,
            sizeof (session->pasvAddress))) {
        // failed to bind
        nErrorCode = errno;
        CONSOLE_LOG("Failed to bind to a new port: %d %s", nErrorCode,
                strerror(nErrorCode));
        bftps_session_close_pasv(session);
        return bftps_command_send_response(session, 451, "\r\n");
    }

    // listen on the socket
    if (0 != listen(session->pasvFd, 1)) {
        // failed to listen
        nErrorCode = errno;
        CONSOLE_LOG("Failed to listen on the socket: %d %s", nErrorCode,
                strerror(nErrorCode));
        bftps_session_close_pasv(session);
        return bftps_command_send_response(session, 451, "\r\n");
    }

    //#ifndef _3DS
    {
        // get the socket address since we requested an ephemeral port
        socklen_t addrlen = sizeof (session->pasvAddress);
        if (0 != getsockname(session->pasvFd, (struct sockaddr*)
                &session->pasvAddress, &addrlen)) {
            // failed to get socket address
            nErrorCode = errno;
            CONSOLE_LOG("Failed to get socket address: %d %s", nErrorCode,
                    strerror(nErrorCode));
            bftps_session_close_pasv(session);
            return bftps_command_send_response(session, 451, "\r\n");
        }
    }
    //#endif

    // we are now listening on the socket
    CONSOLE_LOG("Listening on %s:%u", inet_ntoa(session->pasvAddress.sin_addr),
            ntohs(session->pasvAddress.sin_port));
    session->flags |= BFTPS_SESSION_FLAG_PASV;

    // print the address in the ftp format
    char buffer[INET_ADDRSTRLEN + 10];
    char *p;
    memset(buffer, 0, sizeof (buffer));
    in_port_t port = ntohs(session->pasvAddress.sin_port);
    strcpy(buffer, inet_ntoa(session->pasvAddress.sin_addr));
    sprintf(buffer + strlen(buffer), ",%u,%u", port >> 8, port & 0xFF);
    for (p = buffer; *p; ++p) {
        if (*p == '.')
            *p = ',';
    }

    return bftps_command_send_response(session, 227, "%s\r\n", buffer);        
}

// provide an address for the server to connect to
FTP_DECLARE(PORT) {
    CONSOLE_LOG("PORT %s", args ? args : "");    

    // reset the state
    bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND,
            BFTPS_SESSION_MODE_SET_FLAG_CLOSE_PASV |
            BFTPS_SESSION_MODE_SET_FLAG_CLOSE_DATA);
    session->flags &= ~(BFTPS_SESSION_MODE_SET_FLAG_CLOSE_PASV | 
            BFTPS_SESSION_MODE_SET_FLAG_CLOSE_DATA);

    // dup the args since they are const and we need to change it
    char *addrstr = strdup(args);
    if (addrstr == NULL) {
        return bftps_command_send_response(session, 425, "%s\r\n", strerror(ENOMEM));
    }

    // replace a,b,c,d,e,f with a.b.c.d\0e.f
    int commas = 0;
    char *p, *portstr;
    for (p = addrstr; *p; ++p) {
        if (*p == ',') {
            if (commas != 3)
                *p = '.';
            else {
                *p = '\0';
                portstr = p + 1;
            }
            ++commas;
        }
    }

    // make sure we got the right number of values
    if (commas != 5) {
        free(addrstr);
        
        return bftps_command_send_response(session, 501, "%s\r\n", strerror(EINVAL));
    }

    // parse the address
    struct sockaddr_in addr;
    if (0 == inet_aton(addrstr, &addr.sin_addr)) {
        free(addrstr);        
        return bftps_command_send_response(session, 501, "%s\r\n", strerror(EINVAL));
    }

    // parse the port
    short port = 0;
    unsigned long val = 0;
    for (p = portstr; *p; ++p) {
        if (!isdigit((int) *p)) {
            if (p == portstr || *p != '.' || val > 0xFF) {
                free(addrstr);               
                return bftps_command_send_response(session, 501, "%s\r\n", strerror(EINVAL));
            }
            port <<= 8;
            port += val;
            val = 0;
        } else {
            val *= 10;
            val += *p - '0';
        }
    }

    // validate the port
    if (val > 0xFF || port > 0xFF) {
        free(addrstr);      
        return bftps_command_send_response(session, 501, "%s\r\n", strerror(EINVAL));
    }
    port <<= 8;
    port += val;

    // fill in the address port and family
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    free(addrstr);

    memcpy(&session->dataAddress, &addr, sizeof (addr));

    // we are ready to connect to the client
    session->flags |= BFTPS_SESSION_FLAG_PORT;    
 
    return bftps_command_send_response(session, 200, "OK\r\n");
}

// print work directory

FTP_DECLARE(PWD) {
    CONSOLE_LOG("PWD %s", args ? args : "");
    bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND, 0);

    static char buffer[BFTPS_SESSION_COMMAND_BUFFERSIZE];
    size_t len = sizeof (buffer), i;

    // encode the cwd
    len = strlen(session->cwd);
    char* path = bftps_common_encode_buffer(session->cwd, &len, true);
    if (path != NULL) {
        i = sprintf(buffer, "257 \"");
        if (i + len + 3 > sizeof (buffer)) {
            // buffer will overflow
            free(path);
            if (SUCCEEDED(bftps_command_send_response(session, 550,
                    "unavailable\r\n")))
                bftps_command_send_response(session, 425, "%s\r\n",
                    strerror(EOVERFLOW));
            return EOVERFLOW;
        }
        memcpy(buffer + i, path, len);
        free(path);
        len += i;
        buffer[len++] = '"';
        buffer[len++] = '\r';
        buffer[len++] = '\n';

        return bftps_command_send_response_buffer(session, buffer, len);
    }

    return bftps_command_send_response(session, 425, "%s\r\n",
            strerror(ENOMEM));
}

// terminate ftp session

FTP_DECLARE(QUIT) {
    CONSOLE_LOG("QUIT %s", args ? args : "");

    // disconnect from the client
    bftps_command_send_response(session, 221, "disconnecting\r\n");
    // destroy this session
    bftps_session_mode_set(session, BFTPS_SESSION_MODE_DESTROY, 0); 

    return 0;
}

//restart a transfer - sets file position for a subsequent STOR or RETR operation

FTP_DECLARE(REST) {
    CONSOLE_LOG("REST %s", args ? args : "");

    bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND, 0);

    // make sure an argument is provided
    if (args == NULL) {        
        return bftps_command_send_response(session, 504, "invalid argument\r\n");
    }

    // parse the offset
    const char *p;
    uint64_t pos = 0;
    for (p = args; *p; ++p) {
        if (!isdigit((int) *p)) {            
            return bftps_command_send_response(session, 504, "invalid argument\r\n");
        }

        if (UINT64_MAX / 10 < pos) {            
            return bftps_command_send_response(session, 504, "invalid argument\r\n");
        }

        pos *= 10;

        if (UINT64_MAX - (*p - '0') < pos) {
           return bftps_command_send_response(session, 504, "invalid argument\r\n");
        }

        pos += (*p - '0');
    }

    // set the restart offset
    session->filepos = pos;
    return bftps_command_send_response(session, 200, "OK\r\n");
}

// retrieve a file - Requires a PASV or PORT connection

FTP_DECLARE(RETR) {
    CONSOLE_LOG("RETR %s", args ? args : "");

    // open the file to retrieve
    return bftps_transfer_file(session, args, BFTPS_TRANSFER_FILE_RETR);
}

// remove a directory

FTP_DECLARE(RMD) {
    CONSOLE_LOG("RMD %s", args ? args : "");

    bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND, 0);

    // build the path to remove
    int nErrorCode = 0;
    if (FAILED(nErrorCode = bftps_common_build_path(session, session->cwd, args))) {
        return bftps_command_send_response(session, 553, "%s\r\n", strerror(nErrorCode));
    }

    // remove the directory 
    if (0 != rmdir(session->dataBuffer)) {
        // rmdir error
        nErrorCode = errno;
        CONSOLE_LOG("rmdir: %d %s", nErrorCode, strerror(nErrorCode));
        return bftps_command_send_response(session, 550, "failed to delete directory\r\n");
    }

    bftps_common_update_free_space(session);
    return bftps_command_send_response(session, 250, "OK\r\n");
}

// rename from - Must be followed by RNTO
 
FTP_DECLARE(RNFR) {
    CONSOLE_LOG("RNFR %s", args ? args : ""); 

    bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND, 0);

    // build the path to rename from
    int nErrorCode = 0;
    if (FAILED( nErrorCode = bftps_common_build_path(session, session->cwd, args))) {
        return bftps_command_send_response(session, 553, "%s\r\n", strerror(nErrorCode));
    }

    // make sure the path exists
    struct stat st;
    if (0 != lstat(session->dataBuffer, &st)) {
        // error getting path status
        nErrorCode = errno;
        CONSOLE_LOG("lstat: %d %s", nErrorCode, strerror(nErrorCode));
        return bftps_command_send_response(session, 450, "no such file or directory\r\n");
    }

    // we are ready for RNTO
    session->flags |= BFTPS_SESSION_FLAG_RENAME;
    return bftps_command_send_response(session, 350, "OK\r\n");
}

// rename to - Must be preceded by RNFR

FTP_DECLARE(RNTO) {
    CONSOLE_LOG("RNTO %s", args ? args : "");  
    static char rnfr[BFTPS_SESSION_TRANSFER_BUFFER_SIZE]; // rename-from buffer

    bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND, 0);
    
    // make sure the previous command was RNFR
    if (!(session->flags & BFTPS_SESSION_FLAG_RENAME)) {
       return bftps_command_send_response(session, 503, "Bad sequence of commands\r\n");
    }

    // clear the rename state
    session->flags &= ~BFTPS_SESSION_FLAG_RENAME;

    // copy the RNFR path 
    memcpy(rnfr, session->dataBuffer, BFTPS_SESSION_TRANSFER_BUFFER_SIZE);

    // build the path to rename to
    int nErrorCode = 0;
    if (FAILED(nErrorCode = bftps_common_build_path(session, session->cwd, args))) {
        return bftps_command_send_response(session, 554, "%s\r\n", strerror(nErrorCode));
    }

    // rename the file
    if (0 != rename(rnfr, session->dataBuffer)) {
        // rename failure
        nErrorCode = errno;
        CONSOLE_LOG("rename: %d %s", nErrorCode, strerror(nErrorCode));
        return bftps_command_send_response(session, 550, "failed to rename file/directory\r\n");
    }

    bftps_common_update_free_space(session);
    return bftps_command_send_response(session, 250, "OK\r\n");
}

// get file size

FTP_DECLARE(SIZE) {
    CONSOLE_LOG("SIZE %s", args ? args : "");    

    bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND, 0);

    // build the path to stat
    int nErrorCode = 0;
    if (FAILED(nErrorCode = bftps_common_build_path(session, session->cwd, args))) {
        return bftps_command_send_response(session, 553, "%s\r\n", strerror(nErrorCode));   
    }
    struct stat st;
    int rc = stat(session->dataBuffer, &st);
    if (0 != rc || !S_ISREG(st.st_mode)) {
        return bftps_command_send_response(session, 550, "Could not get file size.\r\n");
    }

    return bftps_command_send_response(session, 213, "%" PRIu64 "\r\n",
            (uint64_t) st.st_size);
}

// get status - If no argument is supplied, and a transfer is occurring, get the
// current transfer status. If no argument is supplied, and no transfer
// is occurring, get the server status. If an argument is supplied, this
// is equivalent to LIST, except the data is sent over the command socket.
FTP_DECLARE(STAT) {
    CONSOLE_LOG("STAT %s", args ? args : "");
    
    time_t uptime = time(NULL) - bftps_start_time();
    int hours = uptime / 3600;
    int minutes = (uptime / 60) % 60;
    int seconds = uptime % 60;

    if (session->mode == BFTPS_SESSION_MODE_DATA_CONNECT) {
        // we are waiting to connect to the client
        return bftps_command_send_response(session, -211, "FTP server status\r\n"
                " Waiting for data connection\r\n"
                "211 End\r\n");
    } else if (session->mode == BFTPS_SESSION_MODE_DATA_TRANSFER) {
        // we are in the middle of a transfer
        return bftps_command_send_response(session, -211, "FTP server status\r\n"
                " Transferred %" PRIu64 " bytes\r\n"
                "211 End\r\n",
                session->filepos);
    }

    if (strlen(args) == 0) {
        /* no argument provided, send the server status */
        return bftps_command_send_response(session, -211, "FTP server status\r\n"
                " Uptime: %02d:%02d:%02d\r\n"
                "211 End\r\n",
                hours, minutes, seconds);
    }

    // argument provided, open the path in STAT mode
    return bftps_transfer_dir(session, args, BFTPS_TRANSFER_DIR_MODE_STAT, false);
}

// store a file - Requires a PASV or PORT connection

FTP_DECLARE(STOR) {
    CONSOLE_LOG("STOU %s", args ? args : "");

    // open the file to store
    return bftps_transfer_file(session, args, BFTPS_TRANSFER_FILE_STOR);
}

// store a unique file

FTP_DECLARE(STOU) {
    CONSOLE_LOG("STOU %s", args ? args : "");

    // we do not support this yet
    bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND, 0);

    return bftps_command_send_response(session, 502, "unavailable\r\n");
}

// set file structure

FTP_DECLARE(STRU) {
    CONSOLE_LOG("STRU %s", args ? args : "");

    bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND, 0);

    // we only support F (no structure) mode
    if (strcasecmp(args, "F") == 0) {        
        return bftps_command_send_response(session, 200, "OK\r\n");
    }

    return bftps_command_send_response(session, 504, "unavailable\r\n");
}

// identify system

FTP_DECLARE(SYST) {
    CONSOLE_LOG("SYST %s", args ? args : "");

    bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND, 0);

    // we are UNIX compliant with 8-bit characters
    return bftps_command_send_response(session, 215, "UNIX Type: L8\r\n");
}

//  transfer mode is always binary

FTP_DECLARE(TYPE) {
    CONSOLE_LOG("TYPE %s", args ? args : "");
    bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND, 0);

    // we transfer data in both mods (ascii when listing dirs, binary for file transfers)
    return bftps_command_send_response(session, 200, "OK\r\n");
}

// provide user name

FTP_DECLARE(USER) {
    CONSOLE_LOG("USER %s", args ? args : "");
    bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND, 0);
    // we accept any user name
    return bftps_command_send_response(session, 230, "OK\r\n");
}