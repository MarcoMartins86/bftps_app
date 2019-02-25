#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#ifdef _3DS
#include <3ds.h>
// TODO check if there is really no lseek64
#define lseek64 lseek
#endif

#include "bftps_transfer_file.h"
#include "bftps_session.h"
#include "bftps_command.h"
#include "bftps_common.h"

#include "macros.h"
#include "file_io.h"

//32 MB
#define BIG_FILE_TRESHOLD 32 * 1024 * 1024 

extern void bftps_file_transfer_store(bftps_session_context_t* session);

// open file for reading for ftp session

int bftps_transfer_file_open_read(bftps_session_context_t *session) {
    int nErrorCode = 0;
    // open file in read mode  
#ifdef _USE_FD_TRANSFER
    session->fileFd = open(session->dataBuffer, O_RDONLY | O_BINARY);
    if (-1 == session->fileFd) {
        nErrorCode = errno;
        CONSOLE_LOG("open '%s': %d %s", session->dataBuffer, nErrorCode, strerror(nErrorCode));
        return nErrorCode;
    }
#else
    session->filep = fopen(session->dataBuffer, "rb");
    if (NULL == session->filep) {
        nErrorCode = errno;
        CONSOLE_LOG("fopen '%s': %d %s", session->dataBuffer, nErrorCode, strerror(nErrorCode));
        return nErrorCode;
    }
#endif
    // get the file size
    struct stat st;
#ifdef _USE_FD_TRANSFER
    if (0 != fstat(session->fileFd, &st)) {
#else
    if (0 != fstat(fileno(session->filep), &st)) {
#endif
        nErrorCode = errno;
        CONSOLE_LOG("fstat '%s': %d %s", session->dataBuffer, nErrorCode, strerror(nErrorCode));
        return nErrorCode;
    }
    session->filesize = st.st_size;

    if (session->filepos != 0) {
#ifdef _USE_FD_TRANSFER
        if (-1 == lseek64(session->fileFd, session->filepos, SEEK_SET)) {
#else
        if (0 != fseek(session->filep, session->filepos, SEEK_SET)) {
#endif
            nErrorCode = errno;
            CONSOLE_LOG("Seeking '%s': %d %s", session->dataBuffer, nErrorCode, strerror(nErrorCode));
            return nErrorCode;
        }
    }
    
    //session->fileBig = session->filesize > BIG_FILE_TRESHOLD;
    
    return 0;
}

// open file for writing for ftp session
int bftps_transfer_file_open_write(bftps_session_context_t *session, bool append) {
    int nErrorCode = 0;
#ifdef _USE_FD_TRANSFER
    int openFlags = O_WRONLY | O_BINARY; // if we want to resume a file transfer this is enough

    if (append) //we want to append to file
        openFlags |= O_APPEND;
    else if (session->filepos == 0) // we want to create a file or truncate an existent one
        openFlags |= O_CREAT | O_TRUNC;


    // open file in write mode    
    session->fileFd = open(session->dataBuffer, openFlags,
            S_IRWXU | S_IRWXG | S_IRWXO);
    if (-1 == session->fileFd) {
        nErrorCode = errno;
        CONSOLE_LOG("open '%s': %d %s", session->dataBuffer, nErrorCode, strerror(nErrorCode));
        return nErrorCode;
    }
#else 
    const char *mode = "wb";

    if (append)
        mode = "ab";
    else if (session->filepos != 0)
        mode = "r+b";

    session->filep = fopen(session->dataBuffer, mode);
    if (NULL == session->filep) {
        nErrorCode = errno;
        CONSOLE_LOG("fopen '%s': %d %s", session->dataBuffer, nErrorCode, strerror(nErrorCode));
        return nErrorCode;
    }

    // it's okay if this fails 
    if (0 != setvbuf(session->filep, session->fileBuffer, _IOFBF, BFTPS_SESSION_FILE_BUFFER_SIZE)) {
        nErrorCode = errno;
        CONSOLE_LOG("setvbuf: %d %s", nErrorCode, strerror(nErrorCode));
    }
#endif
    bftps_common_update_free_space(session);

    // check if this had REST but not APPE
    if (session->filepos != 0 && !append) {
        // seek to the REST offset
#ifdef _USE_FD_TRANSFER
        if (-1 == lseek64(session->fileFd, session->filepos, SEEK_SET)) {
#else
        if (0 != fseek(session->filep, session->filepos, SEEK_SET)) {
#endif
            nErrorCode = errno;
            CONSOLE_LOG("Seeking '%s': %d %s", session->dataBuffer, nErrorCode, strerror(nErrorCode));
            return nErrorCode;
        }
    }

    return 0;
}

// read from an open file for ftp session

static ssize_t bftps_transfer_file_read(bftps_session_context_t *session) {
    // read file at current position
#ifdef _USE_FD_TRANSFER
    ssize_t rc = read(session->fileFd, session->dataBuffer, sizeof (session->dataBuffer));
#else
    ssize_t rc = rc = fread(session->dataBuffer, 1, sizeof (session->dataBuffer), session->filep);
#endif
    if (rc < 0) {
        int nErrorCode = errno;
        CONSOLE_LOG("read: %d %s", nErrorCode, strerror(nErrorCode));
        return -1;
    }

    // adjust file position
    session->filepos += rc;
    
    bftps_file_transfer_store(session);

    return rc;
}

// write to an open file for ftp session

ssize_t bftps_transfer_file_write(bftps_session_context_t *session) {
    // write to file at current position
#ifdef _USE_FD_TRANSFER
    ssize_t rc = write(session->fileFd, session->dataBuffer + session->dataBufferPosition,
            session->dataBufferSize - session->dataBufferPosition);
#else
    ssize_t rc = fwrite(session->dataBuffer + session->dataBufferPosition,
            1, session->dataBufferSize - session->dataBufferPosition,
            session->filep);
#endif
    if (-1 == rc) {
        int nErrorCode = errno;
        CONSOLE_LOG("write: %d %s", nErrorCode, strerror(nErrorCode));
        return -1;
    } else if (0 == rc) {
        CONSOLE_LOG("write: wrote 0 bytes");
    }

    // adjust file position
    session->filepos += rc;

    bftps_common_update_free_space(session);
    bftps_file_transfer_store(session);
    
    return rc;
}

// send a file to the client
bftps_transfer_loop_status_t bftps_transfer_file_retrieve(bftps_session_context_t *session) {
    ssize_t rc;
  /*  
    if (true == session->fileBig) {
        int nErrorCode = 0;
        if (session->fileBigIO == NULL) {
#ifdef _USE_FD_TRANSFER
            if (FAILED(nErrorCode = file_io_init(&session->fileBigIO, file_io_mode_read_write, session->fileFd, session->dataFd)) ||
#else
            if (FAILED(nErrorCode = file_io_init(&session->fileBigIO, file_io_mode_read_write, fileno(session->filep), session->dataFd)) ||
#endif
                    FAILED(nErrorCode = file_io_start_read(session->fileBigIO)) ||
                    FAILED(nErrorCode = file_io_start_write(session->fileBigIO))) {
                CONSOLE_LOG("Failed to init/start file IO: %d %s", nErrorCode, strerror(nErrorCode));
                bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND,
                        BFTPS_SESSION_MODE_SET_FLAG_CLOSE_PASV |
                        BFTPS_SESSION_MODE_SET_FLAG_CLOSE_DATA);
                bftps_command_send_response(session, 451, "Failed to read file\r\n");
                return BFTPS_TRANSFER_LOOP_STATUS_EXIT;
            }       
        }
        
        if (SUCCEEDED(file_io_wait(session->fileBigIO, 150))) {
            bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND,
                    BFTPS_SESSION_MODE_SET_FLAG_CLOSE_PASV |
                    BFTPS_SESSION_MODE_SET_FLAG_CLOSE_DATA);
            return BFTPS_TRANSFER_LOOP_STATUS_EXIT;
        }

        return BFTPS_TRANSFER_LOOP_STATUS_EXIT;
    } else {*/
        if (session->dataBufferPosition == session->dataBufferSize) {
            // we have sent all the data so read some more
            rc = bftps_transfer_file_read(session);
            if (0 >= rc) {
                // can't read any more data
                bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND,
                        BFTPS_SESSION_MODE_SET_FLAG_CLOSE_PASV |
                        BFTPS_SESSION_MODE_SET_FLAG_CLOSE_DATA);
                if (0 > rc)
                    bftps_command_send_response(session, 451, "Failed to read file\r\n");
                else
                    bftps_command_send_response(session, 226, "OK\r\n");
                return BFTPS_TRANSFER_LOOP_STATUS_EXIT;
            }

            // we read some data so reset the session buffer to send
            session->dataBufferPosition = 0;
            session->dataBufferSize = rc;
        }

        int nErrorCode = 0;
        // send any pending data
        rc = send(session->dataFd, session->dataBuffer + session->dataBufferPosition,
                session->dataBufferSize - session->dataBufferPosition, MSG_NOSIGNAL);
        if (0 >= rc) {
            // error sending data
            if (0 > rc) {
                nErrorCode = errno;
                if (nErrorCode == EWOULDBLOCK)
                    return BFTPS_TRANSFER_LOOP_STATUS_EXIT;
                CONSOLE_LOG("send: %d %s", nErrorCode, strerror(nErrorCode));
            } else
            {
                CONSOLE_LOG("send: %d %s", ECONNRESET, strerror(ECONNRESET));
            }

            bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND,
                    BFTPS_SESSION_MODE_SET_FLAG_CLOSE_PASV |
                    BFTPS_SESSION_MODE_SET_FLAG_CLOSE_DATA);
            bftps_command_send_response(session, 426, "Connection broken during transfer\r\n");
            return BFTPS_TRANSFER_LOOP_STATUS_EXIT;
        }

        // we can try to send more data
        session->dataBufferPosition += rc;
        return BFTPS_TRANSFER_LOOP_STATUS_CONTINUE;
    //}
}

// store a file from the client
bftps_transfer_loop_status_t bftps_transfer_file_store(bftps_session_context_t *session) {
    
    ssize_t rc;
    int nErrorCode = 0;
    if (session->dataBufferPosition == session->dataBufferSize) {
        // we have written all the received data, so try to get some more
        rc = recv(session->dataFd, session->dataBuffer, sizeof (session->dataBuffer),
                MSG_DONTWAIT | MSG_NOSIGNAL);
        if (0>= rc) {
            // can't read any more data
            if (0 > rc) {
                nErrorCode = errno;
                if (nErrorCode == EWOULDBLOCK)
                    return BFTPS_TRANSFER_LOOP_STATUS_EXIT;
                CONSOLE_LOG("recv: %d %s", nErrorCode, strerror(nErrorCode));
            }

            bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND,
                    BFTPS_SESSION_MODE_SET_FLAG_CLOSE_PASV |
                    BFTPS_SESSION_MODE_SET_FLAG_CLOSE_DATA);

            if (rc == 0)
                bftps_command_send_response(session, 226, "OK\r\n");
            else
                bftps_command_send_response(session, 426, "Connection broken during transfer\r\n");
            return BFTPS_TRANSFER_LOOP_STATUS_EXIT;
        }

        // we received some data so reset the session buffer to write
        session->dataBufferPosition = 0;
        session->dataBufferSize = rc;
    }

    rc = bftps_transfer_file_write(session);
    if (0 >= rc) {
        // error writing data
        bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND,
                    BFTPS_SESSION_MODE_SET_FLAG_CLOSE_PASV |
                    BFTPS_SESSION_MODE_SET_FLAG_CLOSE_DATA);
        bftps_command_send_response(session, 451, "Failed to write file\r\n");
        return BFTPS_TRANSFER_LOOP_STATUS_EXIT;
    }

    // we can try to receive more data
    session->dataBufferPosition += rc;
    return BFTPS_TRANSFER_LOOP_STATUS_CONTINUE;
}

// Transfer a file
int bftps_transfer_file(bftps_session_context_t *session, const char *args,
        bftps_transfer_file_mode_t mode) {

    // build the path of the file to transfer
    int nErrorCode = 0;
    if (FAILED(nErrorCode = bftps_common_build_path(session, session->cwd, args))) {
        bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND,
                BFTPS_SESSION_MODE_SET_FLAG_CLOSE_PASV |
                BFTPS_SESSION_MODE_SET_FLAG_CLOSE_DATA);        
        return bftps_command_send_response(session, 553, "%s\r\n", strerror(nErrorCode));;
    }

    // open the file for retrieving or storing
    if (mode == BFTPS_TRANSFER_FILE_RETR) 
        nErrorCode = bftps_transfer_file_open_read(session);
    else
        nErrorCode = bftps_transfer_file_open_write(session, mode == BFTPS_TRANSFER_FILE_APPE);

    if (FAILED(nErrorCode)) {
        // error opening the file
        bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND,
                BFTPS_SESSION_MODE_SET_FLAG_CLOSE_PASV |
                BFTPS_SESSION_MODE_SET_FLAG_CLOSE_DATA);
        return bftps_command_send_response(session, 450, "failed to open file\r\n");
    }

    if (session->flags & (BFTPS_SESSION_FLAG_PORT | BFTPS_SESSION_FLAG_PASV)) {
        bftps_session_mode_set(session, BFTPS_SESSION_MODE_DATA_CONNECT, 
                BFTPS_SESSION_MODE_SET_FLAG_CLOSE_DATA);

        if (session->flags & BFTPS_SESSION_FLAG_PORT) {
            // setup connection
            if (FAILED(bftps_session_connect(session))) {
                // error connecting
                bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND,
                BFTPS_SESSION_MODE_SET_FLAG_CLOSE_PASV |
                BFTPS_SESSION_MODE_SET_FLAG_CLOSE_DATA);
                return bftps_command_send_response(session, 425, "can't open data connection\r\n");
            }
        }

        // set up the transfer
        session->flags &= ~(BFTPS_SESSION_FLAG_RECV | BFTPS_SESSION_FLAG_SEND);
        if (mode == BFTPS_TRANSFER_FILE_RETR) {
            session->flags |= BFTPS_SESSION_FLAG_SEND;
            session->transfer = bftps_transfer_file_retrieve;
        } else {
            session->flags |= BFTPS_SESSION_FLAG_RECV;
            session->transfer = bftps_transfer_file_store;
        }

        session->dataBufferPosition = 0;
        session->dataBufferSize = 0;
        session->filenameRefresh = true;
        strncpy(session->filename, session->dataBuffer, sizeof(session->filename));

        bftps_file_transfer_store(session);

        return 0;
    }

    bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND,
                BFTPS_SESSION_MODE_SET_FLAG_CLOSE_PASV |
                BFTPS_SESSION_MODE_SET_FLAG_CLOSE_DATA);
    return bftps_command_send_response(session, 503, "Bad sequence of commands\r\n");
}