#include <errno.h>
#include <pwd.h>
#include <grp.h>
#include <sys/socket.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <dirent.h>
#ifdef _3DS
#include <3ds.h>
//#define lstat stat
#endif

#include "bftps_transfer_dir.h"
#include "bftps_command.h"
#include "bftps_common.h"

// fill directory entry

int bftps_transfer_dir_fill_dirent_type(bftps_session_context_t *session,
        const struct stat *st, const char *path, size_t len, const char *type) {
    session->dataBufferSize = 0;

    if (session->dirMode == BFTPS_TRANSFER_DIR_MODE_MLSD
            || session->dirMode == BFTPS_TRANSFER_DIR_MODE_MLST) {
        if (session->dirMode == BFTPS_TRANSFER_DIR_MODE_MLST)
            session->dataBuffer[session->dataBufferSize++] = ' ';

        if (session->mlstFlags & BFTPS_TRANSFER_DIR_MLST_TYPE) {
            // type fact 
            if (!type) {
                type = "???";
                if (S_ISREG(st->st_mode))
                    type = "file";
                else if (S_ISDIR(st->st_mode))
                    type = "dir";
#if !defined(_3DS) && !defined(__SWITCH__)
                else if (S_ISLNK(st->st_mode))
                    type = "os.unix=symlink";
                else if (S_ISCHR(st->st_mode))
                    type = "os.unix=character";
                else if (S_ISBLK(st->st_mode))
                    type = "os.unix=block";
                else if (S_ISFIFO(st->st_mode))
                    type = "os.unix=fifo";
                else if (S_ISSOCK(st->st_mode))
                    type = "os.unix=socket";
#endif
            }

            session->dataBufferSize += sprintf(session->dataBuffer +
                    session->dataBufferSize, "Type=%s;", type);
        }

        if (session->mlstFlags & BFTPS_TRANSFER_DIR_MLST_SIZE) {
            // size fact
            session->dataBufferSize += sprintf(session->dataBuffer +
                    session->dataBufferSize, "Size=%lld;", (signed long long) st->st_size);
        }

        if (session->mlstFlags & BFTPS_TRANSFER_DIR_MLST_MODIFY) {
            // mtime fact
            struct tm *tm = gmtime(&st->st_mtime);
            if (tm == NULL)
                return errno;

            size_t result = strftime(session->dataBuffer + session->dataBufferSize,
                    sizeof (session->dataBuffer) - session->dataBufferSize,
                    "Modify=%Y%m%d%H%M%S;", tm);
            if (result == 0)
                return EOVERFLOW;

            session->dataBufferSize += result;
        }

        if (session->mlstFlags & BFTPS_TRANSFER_DIR_MLST_PERM) {
            //permission fact
            strcpy(session->dataBuffer + session->dataBufferSize, "Perm=");
            session->dataBufferSize += strlen("Perm=");

            // append permission
            if (S_ISREG(st->st_mode) && (st->st_mode & S_IWUSR))
                session->dataBuffer[session->dataBufferSize++] = 'a';

            // create permission
            if (S_ISDIR(st->st_mode) && (st->st_mode & S_IWUSR))
                session->dataBuffer[session->dataBufferSize++] = 'c';

            // delete permission
            // TODO should check if parent directory allow us to delete the file
            session->dataBuffer[session->dataBufferSize++] = 'd';

            // chdir permission
            if (S_ISDIR(st->st_mode) && (st->st_mode & S_IXUSR))
                session->dataBuffer[session->dataBufferSize++] = 'e';

            // rename permission
            // TODO should check if parent directory allow us to rename the file
            session->dataBuffer[session->dataBufferSize++] = 'f';

            // list permission
            if (S_ISDIR(st->st_mode) && (st->st_mode & S_IRUSR))
                session->dataBuffer[session->dataBufferSize++] = 'l';

            // mkdir permission
            if (S_ISDIR(st->st_mode) && (st->st_mode & S_IWUSR))
                session->dataBuffer[session->dataBufferSize++] = 'm';

            // delete permission
            if (S_ISDIR(st->st_mode) && (st->st_mode & S_IWUSR))
                session->dataBuffer[session->dataBufferSize++] = 'p';

            // read permission
            if (S_ISREG(st->st_mode) && (st->st_mode & S_IRUSR))
                session->dataBuffer[session->dataBufferSize++] = 'r';

            // write permission
            if (S_ISREG(st->st_mode) && (st->st_mode & S_IWUSR))
                session->dataBuffer[session->dataBufferSize++] = 'w';

            session->dataBuffer[session->dataBufferSize++] = ';';
        }

        if (session->mlstFlags & BFTPS_TRANSFER_DIR_MLST_UNIX_MODE) {
            // unix mode fact
            mode_t mask = S_IRWXU | S_IRWXG | S_IRWXO | S_ISVTX | S_ISGID | S_ISUID;
            session->dataBufferSize +=
                    sprintf(session->dataBuffer + session->dataBufferSize,
                    "UNIX.mode=0%lo;", (unsigned long) (st->st_mode & mask));
        }

        // make sure space precedes name
        if (session->dataBuffer[session->dataBufferSize - 1] != ' ')
            session->dataBuffer[session->dataBufferSize++] = ' ';
    } else if (session->dirMode != BFTPS_TRANSFER_DIR_MODE_NLST) {
        if (session->dirMode == BFTPS_TRANSFER_DIR_MODE_STAT)
            session->dataBuffer[session->dataBufferSize++] = ' ';
        
        static char NA[] = "N.A.";
        char * owner = NA;
        char * group = NA;
#ifdef __linux
        struct passwd *pw = getpwuid(st->st_uid);
        struct group  *gr = getgrgid(st->st_gid);
        if(pw && gr)
        {
            owner = pw->pw_name;
            group = gr->gr_name;
        }
#endif
        
        // perms nlinks owner group size
        session->dataBufferSize +=
                sprintf(session->dataBuffer + session->dataBufferSize,
                "%c%c%c%c%c%c%c%c%c%c %lu %s %s %lld ",
                S_ISREG(st->st_mode) ? '-' :
                S_ISDIR(st->st_mode) ? 'd' :
#ifdef __linux__
                S_ISLNK(st->st_mode) ? 'l' :
                S_ISCHR(st->st_mode) ? 'c' :
                S_ISBLK(st->st_mode) ? 'b' :
                S_ISFIFO(st->st_mode) ? 'p' :
                S_ISSOCK(st->st_mode) ? 's' :
#endif
                '?',
                st->st_mode & S_IRUSR ? 'r' : '-',
                st->st_mode & S_IWUSR ? 'w' : '-',
                st->st_mode & S_IXUSR ? 'x' : '-',
                st->st_mode & S_IRGRP ? 'r' : '-',
                st->st_mode & S_IWGRP ? 'w' : '-',
                st->st_mode & S_IXGRP ? 'x' : '-',
                st->st_mode & S_IROTH ? 'r' : '-',
                st->st_mode & S_IWOTH ? 'w' : '-',
                st->st_mode & S_IXOTH ? 'x' : '-',
                (unsigned long) st->st_nlink,
                owner,
                group,
                (signed long long) st->st_size);

        // timestamp
        struct tm *tm = gmtime(&st->st_mtime);
        if (tm) {
            const char *fmt = "%b %e %Y ";
            if (session->timestamp > st->st_mtime
                    && session->timestamp - st->st_mtime < (60 * 60 * 24 * 365 / 2)) {
                fmt = "%b %e %H:%M ";
            }

            session->dataBufferSize +=
                    strftime(session->dataBuffer + session->dataBufferSize,
                    sizeof (session->dataBuffer) - session->dataBufferSize,
                    fmt, tm);
        } else {
            session->dataBufferSize +=
                    sprintf(session->dataBuffer + session->dataBufferSize, "Jan 1 1970 ");
        }
    }

    if (session->dataBufferSize + len + 2 > sizeof (session->dataBuffer)) {
        // buffer will overflow 
        return EOVERFLOW;
    }

    // copy path 
    memcpy(session->dataBuffer + session->dataBufferSize, path, len);
    len = session->dataBufferSize + len;
    session->dataBuffer[len++] = '\r';
    session->dataBuffer[len++] = '\n';
    session->dataBufferSize = len;

    return 0;
}

int bftps_transfer_dir_fill_dirent(bftps_session_context_t *session, 
        const struct stat *st,  const char *path, size_t len)
{
  return bftps_transfer_dir_fill_dirent_type(session, st, path, len, NULL);
}

int bftps_transfer_dir_fill_dirent_cdir(bftps_session_context_t *session,
        const char *path)
{
  struct stat st;
  int result = stat(path, &st);
  // double-check this was a directory
  if(result == 0 && !S_ISDIR(st.st_mode))
  {
    // shouldn't happen but just in case
    result = ENOTDIR;
  }
  if(result != 0)
    return result;

  // encode \n in path
  size_t len = strlen(path);
  char *buffer = bftps_common_encode_buffer(path, &len, false);
  if(!buffer)
    return ENOMEM;

  // fill dirent with listed directory as type=cdir
  result = bftps_transfer_dir_fill_dirent_type(session, &st, buffer, len, "cdir");
  free(buffer);

  return result;
}

// transfer a directory listing

bftps_transfer_loop_status_t bftps_transfer_dir_list(
        bftps_session_context_t *session) {
    // check if we sent all available data
    if (session->dataBufferPosition == session->dataBufferSize) {
        struct stat st;
        // check transfer dir type
        int nResponseCode = 0;
        if (session->dirMode == BFTPS_TRANSFER_DIR_MODE_STAT)
            nResponseCode = 213;
        else
            nResponseCode = 226;

        // check if this was for a file
        if (session->dir == NULL) {
            // we already sent the file's listing
            bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND,
                    BFTPS_SESSION_MODE_SET_FLAG_CLOSE_DATA |
                    BFTPS_SESSION_MODE_SET_FLAG_CLOSE_PASV);
            bftps_command_send_response(session, nResponseCode, "OK\r\n");
            return BFTPS_TRANSFER_LOOP_STATUS_EXIT;
        }

        // get the next directory entry
        struct dirent* directoryEntry = readdir(session->dir);
        if (directoryEntry == NULL) {
            // we have exhausted the directory listing
            bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND,
                    BFTPS_SESSION_MODE_SET_FLAG_CLOSE_DATA |
                    BFTPS_SESSION_MODE_SET_FLAG_CLOSE_PASV);
            bftps_command_send_response(session, nResponseCode, "OK\r\n");
            return BFTPS_TRANSFER_LOOP_STATUS_EXIT;
        }

        // TODO I think we are supposed to return entries for . and ..
        if (strcmp(directoryEntry->d_name, ".") == 0 ||
                strcmp(directoryEntry->d_name, "..") == 0)
            return BFTPS_TRANSFER_LOOP_STATUS_CONTINUE;

        // check if this was a NLST
        if (session->dirMode == BFTPS_TRANSFER_DIR_MODE_NLST) {
            // NLST gives the whole path name            
            int nErrorCode;
            if (SUCCEEDED(nErrorCode = bftps_common_build_path(session,
                    session->lwd, directoryEntry->d_name))) {
                // encode \n in path
                size_t len = session->dataBufferSize;
                char *buffer = bftps_common_encode_buffer(
                        session->dataBuffer, &len, false);
                if (buffer != NULL) {
                    // copy to the session buffer to send
                    memcpy(session->dataBuffer, buffer, len);
                    free(buffer);
                    session->dataBuffer[len++] = '\r';
                    session->dataBuffer[len++] = '\n';
                    session->dataBufferSize = len;
                }
            } else {
                CONSOLE_LOG("Failed to build path: %d %s", nErrorCode, strerror(nErrorCode));
            }
        } else {
#ifdef _3DS
            // the sdmc directory entry already has the type and size, so no need to do a slow stat
            u32 magic = *(u32*) session->dir->dirData->dirStruct;
            int nErrorCode = 0;
            if (magic == ARCHIVE_DIRITER_MAGIC) {
                archive_dir_t *dir = (archive_dir_t*) session->dir->dirData->dirStruct;
                FS_DirectoryEntry *entry = &dir->entry_data[dir->index];

                if (entry->attributes & FS_ATTRIBUTE_DIRECTORY)
                    st.st_mode = S_IFDIR | S_IRUSR | S_IRGRP | S_IROTH;
                else
                    st.st_mode = S_IFREG | S_IRUSR | S_IRGRP | S_IROTH;

                if (!(entry->attributes & FS_ATTRIBUTE_READ_ONLY))
                    st.st_mode |= S_IWUSR | S_IWGRP | S_IWOTH;

                st.st_size = entry->fileSize;
                st.st_mtime = 0;

                bool getmtime = true;
                if (session->dirMode == BFTPS_TRANSFER_DIR_MODE_MLSD
                        || session->dirMode == BFTPS_TRANSFER_DIR_MODE_MLST) {
                    if (!(session->mlstFlags & BFTPS_TRANSFER_DIR_MLST_MODIFY))
                        getmtime = false;
                } else if (session->dirMode == BFTPS_TRANSFER_DIR_MODE_NLST)
                    getmtime = false;
                
                if (FAILED(nErrorCode = bftps_common_build_path(session, session->lwd, directoryEntry->d_name)))
                {
                    CONSOLE_LOG("build_path: %d %s", nErrorCode, strerror(nErrorCode));
                }
                else if (getmtime) {
                    uint64_t mtime = 0;
                    if (R_FAILED(nErrorCode = archive_getmtime(session->dataBuffer, &mtime)))
                    {
                        CONSOLE_LOG("archive_getmtime '%s': 0x%x", session->dataBuffer, nErrorCode);
                    }
                    else
                        st.st_mtime = mtime;
                }
            } else {
                // lstat the entry
                if (FAILED(nErrorCode = bftps_common_build_path(session, session->lwd, directoryEntry->d_name)))
                {
                    CONSOLE_LOG("build_path: %d %s", nErrorCode, strerror(nErrorCode));
                }
                else if (FAILED(lstat(session->dataBuffer, &st)))
                {
                    nErrorCode = errno;
                    CONSOLE_LOG("lstat '%s': %d %s", session->dataBuffer, nErrorCode, strerror(nErrorCode));
                }

                if (FAILED(nErrorCode)) {
                    // an error occurred
                    bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND,
                    BFTPS_SESSION_MODE_SET_FLAG_CLOSE_DATA |
                    BFTPS_SESSION_MODE_SET_FLAG_CLOSE_PASV);
                    bftps_command_send_response(session, 550, "unavailable\r\n");
                    return BFTPS_TRANSFER_LOOP_STATUS_EXIT;
                }
            }
#else
            // lstat the entry
            int nErrorCode;
            if (FAILED(nErrorCode = bftps_common_build_path(session, session->lwd,
                    directoryEntry->d_name))) {
                CONSOLE_LOG("Failed to build path: %d %s", nErrorCode, strerror(nErrorCode));
            } else if (FAILED(nErrorCode = lstat(session->dataBuffer, &st))) {
                nErrorCode = errno;
                CONSOLE_LOG("Failed lstat: %d %s", nErrorCode, strerror(nErrorCode));
            }

            if (FAILED(nErrorCode)) {
                // an error occurred
                bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND,
                        BFTPS_SESSION_MODE_SET_FLAG_CLOSE_PASV |
                        BFTPS_SESSION_MODE_SET_FLAG_CLOSE_DATA);
                bftps_command_send_response(session, 550, "unavailable\r\n");
                return BFTPS_TRANSFER_LOOP_STATUS_EXIT;
            }
#endif
            // encode \n in path
            size_t len = strlen(directoryEntry->d_name);
            char* buffer = bftps_common_encode_buffer(directoryEntry->d_name, &len, false);
            if (buffer != NULL) {
                nErrorCode = bftps_transfer_dir_fill_dirent(session, &st, buffer, len);
                free(buffer);
                if (FAILED(nErrorCode)) {
                    bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND,
                            BFTPS_SESSION_MODE_SET_FLAG_CLOSE_PASV |
                            BFTPS_SESSION_MODE_SET_FLAG_CLOSE_DATA);
                    bftps_command_send_response(session, 425, "%s\r\n",
                            strerror(nErrorCode));
                    return BFTPS_TRANSFER_LOOP_STATUS_EXIT;
                }
            } else
                session->dataBufferSize = 0;
        }
        session->dataBufferPosition = 0;
    }

    // send any pending data
    ssize_t result = send(session->dataFd, session->dataBuffer +
            session->dataBufferPosition, session->dataBufferSize -
            session->dataBufferPosition, MSG_NOSIGNAL);
    if (result <= 0) {
        // error sending data
        if (result < 0) {
            int nErrorCode = errno;
            if (nErrorCode == EWOULDBLOCK)
                return BFTPS_TRANSFER_LOOP_STATUS_EXIT; //we will retry in next poll
            CONSOLE_LOG("Failed to send: %d %s", nErrorCode, strerror(nErrorCode));
        } else
        {
            CONSOLE_LOG("Failed to send: %d %s", ECONNRESET, strerror(ECONNRESET));
        }
        bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND,
                BFTPS_SESSION_MODE_SET_FLAG_CLOSE_PASV |
                BFTPS_SESSION_MODE_SET_FLAG_CLOSE_DATA);
        bftps_command_send_response(session, 426, "Connection broken during transfer\r\n");
        return BFTPS_TRANSFER_LOOP_STATUS_EXIT;
    }

    // we can try to send more data
    session->dataBufferPosition += result;
    return BFTPS_TRANSFER_LOOP_STATUS_CONTINUE;
}

int bftps_transfer_dir(bftps_session_context_t *session, const char *args,
        bftps_transfer_dir_mode_t mode, bool workaround) {
    // set up the transfer
    session->dirMode = mode;
    session->flags &= ~BFTPS_SESSION_FLAG_RECV;
    session->flags |= BFTPS_SESSION_FLAG_SEND;

    session->transfer = bftps_transfer_dir_list;
    session->dataBufferSize = 0;
    session->dataBufferPosition = 0;
    int nErrorCode = 0;
    
    if (strlen(args) > 0) {
        // an argument was provided
        
        if (FAILED(nErrorCode = bftps_common_build_path(session, session->cwd, args))) {
            // error building path
            bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND,
                    BFTPS_SESSION_MODE_SET_FLAG_CLOSE_PASV |
                    BFTPS_SESSION_MODE_SET_FLAG_CLOSE_DATA);
            return bftps_command_send_response(session, 550, "%s\r\n", strerror(nErrorCode));
        }

        // check if this is a directory
        session->dir = opendir(session->dataBuffer);
        if (session->dir == NULL) {            
            char * buffer;
            size_t len;
            // not a directory; check if it is a file
            struct stat st;
            int result = stat(session->dataBuffer, &st);
            if (result != 0) {
                // error getting stat
                nErrorCode = errno;
                // work around broken clients that think LIST -a is valid
                if (workaround && mode == BFTPS_TRANSFER_DIR_MODE_LIST) {
                    //TODO I don't think we need to dup the arg
                    if (args[0] == '-' && (args[1] == 'a' || args[1] == 'l')) {                        
                        if (args[2] == 0)
                            buffer = strdup(args + 2);
                        else
                            buffer = strdup(args + 3);

                        if (buffer != NULL) {
                            bftps_transfer_dir(session, buffer, mode, false);
                            free(buffer);
                            return 0;
                        }

                        nErrorCode = ENOMEM;
                    }
                }

                bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND,
                        BFTPS_SESSION_MODE_SET_FLAG_CLOSE_PASV |
                        BFTPS_SESSION_MODE_SET_FLAG_CLOSE_DATA);
                return bftps_command_send_response(session, 550, "%s\r\n", strerror(nErrorCode));
            } else if (mode == BFTPS_TRANSFER_DIR_MODE_MLSD) {
                // specified file instead of directory for MLSD
                bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND,
                        BFTPS_SESSION_MODE_SET_FLAG_CLOSE_PASV |
                        BFTPS_SESSION_MODE_SET_FLAG_CLOSE_DATA);
                return bftps_command_send_response(session, 501, "%s\r\n", strerror(EINVAL));
            } else if (mode == BFTPS_TRANSFER_DIR_MODE_NLST) {
                // NLST uses full path name
                len = session->dataBufferSize;
                buffer = bftps_common_encode_buffer(session->dataBuffer, &len, false);
            } else {
                // everything else uses base name
                const char *base = strrchr(session->dataBuffer, '/') + 1;
                len = strlen(base);
                buffer = bftps_common_encode_buffer(base, &len, false);
            }

            if (buffer) {
                nErrorCode = bftps_transfer_dir_fill_dirent(session, &st, buffer, len);
                free(buffer);
            } else
                nErrorCode = ENOMEM;

            if (FAILED(nErrorCode)) {
                bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND,
                        BFTPS_SESSION_MODE_SET_FLAG_CLOSE_PASV |
                        BFTPS_SESSION_MODE_SET_FLAG_CLOSE_DATA);
                return bftps_command_send_response(session, 550, "%s\r\n", strerror(nErrorCode));
            }
        } else {
            // it was a directory, so set it as the lwd
            memcpy(session->lwd, session->dataBuffer, session->dataBufferSize);
            session->lwd[session->dataBufferSize] = 0;
            session->dataBufferSize = 0;

            if (session->dirMode == BFTPS_TRANSFER_DIR_MODE_MLSD
                    && (session->mlstFlags & BFTPS_TRANSFER_DIR_MLST_TYPE)) {
                // send this directory as type=cdir
                if (FAILED(nErrorCode = bftps_transfer_dir_fill_dirent_cdir(
                        session, session->lwd))) {
                    bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND,
                        BFTPS_SESSION_MODE_SET_FLAG_CLOSE_PASV |
                        BFTPS_SESSION_MODE_SET_FLAG_CLOSE_DATA);
                        return bftps_command_send_response(session, 550,
                                "%s\r\n", strerror(nErrorCode));
                }
            }
        }
    } else if (FAILED(nErrorCode = bftps_session_open_cwd(session))) {
        // no argument, but opening cwd failed
        bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND,
                        BFTPS_SESSION_MODE_SET_FLAG_CLOSE_PASV |
                        BFTPS_SESSION_MODE_SET_FLAG_CLOSE_DATA);
                        return bftps_command_send_response(session, 550,
                                "%s\r\n", strerror(nErrorCode));
    } else {
        // set the cwd as the lwd
        strcpy(session->lwd, session->cwd);
        session->dataBufferSize = 0;

        if (session->dirMode ==  BFTPS_TRANSFER_DIR_MODE_MLSD
                && (session->mlstFlags & BFTPS_TRANSFER_DIR_MLST_TYPE)) {
            // send this directory as type=cdir
            if (FAILED(nErrorCode = bftps_transfer_dir_fill_dirent_cdir(session, session->lwd))) {
                bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND,
                        BFTPS_SESSION_MODE_SET_FLAG_CLOSE_PASV |
                        BFTPS_SESSION_MODE_SET_FLAG_CLOSE_DATA);
                        return bftps_command_send_response(session, 550,
                                "%s\r\n", strerror(nErrorCode));
            }
        }
    }

    if (mode == BFTPS_TRANSFER_DIR_MODE_MLST || 
            mode == BFTPS_TRANSFER_DIR_MODE_STAT) {
        // this is a little different; we have to send the data over the command socket
        bftps_session_mode_set(session, BFTPS_SESSION_MODE_DATA_TRANSFER,
                BFTPS_SESSION_MODE_SET_FLAG_CLOSE_PASV | 
                BFTPS_SESSION_MODE_SET_FLAG_CLOSE_DATA);
        session->dataFd = session->commandFd;
        session->flags |= BFTPS_SESSION_FLAG_SEND;        
        return bftps_command_send_response(session, -213, "Status\r\n");
    } else if (session->flags & (BFTPS_SESSION_FLAG_PORT | BFTPS_SESSION_FLAG_PASV)) {
        bftps_session_mode_set(session, BFTPS_SESSION_MODE_DATA_CONNECT,
                BFTPS_SESSION_MODE_SET_FLAG_CLOSE_DATA);

        if (session->flags & BFTPS_SESSION_FLAG_PORT) {
            // setup connection
            if (FAILED(nErrorCode = bftps_session_connect(session))) {
                // error connecting
                bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND,
                BFTPS_SESSION_MODE_SET_FLAG_CLOSE_PASV | 
                BFTPS_SESSION_MODE_SET_FLAG_CLOSE_DATA);
                return bftps_command_send_response(session, 425,
                        "can't open data connection\r\n");
            }
        }

        return 0;
    }

    // we must have got LIST/MLSD/MLST/NLST without a preceding PORT or PASV
    bftps_session_mode_set(session, BFTPS_SESSION_MODE_COMMAND, BFTPS_SESSION_MODE_SET_FLAG_CLOSE_PASV
            | BFTPS_SESSION_MODE_SET_FLAG_CLOSE_DATA);
    return bftps_command_send_response(session, 503, "Bad sequence of commands\r\n");
}