
#ifndef BFTPS_SESSION_H
#define BFTPS_SESSION_H

#include <dirent.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "bftps_transfer_dir.h"
#include "bftps_socket.h"
#include "macros.h"
#include "bool.h"
#include "file_io.h"

#define BFTPS_SESSION_COMMUNICATION_BUFFER_SIZE BFTPS_SOCKET_BUFFER_SIZE
#define BFTPS_SESSION_TRANSFER_BUFFER_SIZE BFTPS_SOCKET_BUFFER_SIZE
#define BFTPS_SESSION_FILE_BUFFER_SIZE 2*BFTPS_SOCKET_BUFFER_SIZE

#ifdef __cplusplus
extern "C" {
#endif

    typedef enum {
        BFTPS_SESSION_FLAG_BINARY = BIT(0), /* data transfers in binary mode */
        BFTPS_SESSION_FLAG_PASV = BIT(1), /* have pasv_addr ready for data transfer command */
        BFTPS_SESSION_FLAG_PORT = BIT(2), /* have peer_addr ready for data transfer command */
        BFTPS_SESSION_FLAG_RECV = BIT(3), /* data transfer in source mode */
        BFTPS_SESSION_FLAG_SEND = BIT(4), /* data transfer in sink mode */
        BFTPS_SESSION_FLAG_RENAME = BIT(5), /* last command was RNFR and buffer contains path */
        BFTPS_SESSION_FLAG_URGENT = BIT(6), /* in telnet urgent mode */
    } bftps_session_flags_t;

    typedef enum {
        BFTPS_SESSION_MODE_INVALID,
        BFTPS_SESSION_MODE_COMMAND,
        BFTPS_SESSION_MODE_DATA_CONNECT,
        BFTPS_SESSION_MODE_DATA_TRANSFER,        
        BFTPS_SESSION_MODE_DESTROY
    } bftps_session_mode_t;

    typedef enum {
        BFTPS_SESSION_MODE_SET_FLAG_CLOSE_PASV = BIT(0), // Close the pasv_fd
        BFTPS_SESSION_MODE_SET_FLAG_CLOSE_DATA = BIT(1), // Close the data_fd
    } bftps_session_mode_set_flags_t;

    typedef struct _bftps_session_context_t{
        char cwd[MAX_PATH]; /* current working directory */
        char lwd[MAX_PATH];  /* list working directory */
        char commandBuffer[BFTPS_SESSION_COMMUNICATION_BUFFER_SIZE]; /* communication buffer */
        size_t commandBufferSize; /* length of communication buffer */
        int commandFd; /* socket for command connection */
        bftps_session_mode_t mode; /* session state */
        bftps_session_flags_t flags; /* session flags */
        time_t timestamp; /* time from last command */
        struct sockaddr_in pasvAddress;  /* listen address for PASV connection */
        int pasvFd; /* listen socket for PASV */
        bftps_transfer_loop_status_t (*transfer)(struct _bftps_session_context_t*);  /* data transfer callback */
        bftps_transfer_dir_mode_t dirMode; /* dir transfer mode */
        struct sockaddr_in dataAddress;  /* client address for data connection */
        int dataFd;    /* socket for data transfer */
        char dataBuffer[BFTPS_SESSION_TRANSFER_BUFFER_SIZE]; /* persistent data between callbacks */
        size_t dataBufferPosition; /* persistent buffer position between callbacks */
        size_t dataBufferSize; /* persistent buffer size between callbacks */
        DIR *dir; /* persistent open directory pointer between callbacks */
        bftps_transfer_dir_mlst_flags_t mlstFlags; /* session MLST flags */
        //bool fileBig; /* check if it is a big file or small */
        //file_io_context_t* fileBigIO; /* with big files we use this */
        bool filenameRefresh; /* this will be used later to store file transfer info */
        char filename[MAX_PATH]; /* where we will save the filename */
#ifdef _USE_FD_TRANSFER
        int fileFd; /* file descriptor for the open file being transferred */
#else
        FILE* filep;
        char fileBuffer[BFTPS_SESSION_FILE_BUFFER_SIZE]; /* stdio file buffer */
#endif
        uint64_t filepos; /* persistent file position between callbacks */
        uint64_t filesize; /* persistent file size between callbacks */ 
        struct _bftps_session_context_t* next;
    } bftps_session_context_t;

    extern int bftps_session_init(bftps_session_context_t** p_session, int fd_listen);
    extern int bftps_session_destroy(bftps_session_context_t* session);
    extern int bftps_session_accept(bftps_session_context_t *session);
    extern int bftps_session_connect(bftps_session_context_t *session);
    extern int bftps_session_transfer(bftps_session_context_t *session);
    extern int bftps_session_open_cwd(bftps_session_context_t *session);
    extern int bftps_session_close_pasv(bftps_session_context_t *session);
    extern int bftps_session_close_cmd(bftps_session_context_t *session);
    extern int bftps_session_mode_set(bftps_session_context_t* session,
            bftps_session_mode_t mode, bftps_session_mode_set_flags_t flags);
    extern int bftps_session_poll(bftps_session_context_t* session);

#ifdef __cplusplus
}
#endif

#endif /* BFTPS_SESSION_H */

