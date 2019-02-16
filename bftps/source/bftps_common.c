#include <stdlib.h>
#include <errno.h>

#include "bftps_common.h"
#include "macros.h"

void bftps_common_decode_buffer(char *path, size_t len) {
    size_t i;

    // decode \0
    for (i = 0; i < len; ++i) {
        /* this is an encoded \n */
        if (path[i] == '\0')
            path[i] = '\n';
    }
}

char* bftps_common_encode_buffer(const char *path, size_t *len, bool quotes) {
    bool enc = false;
    size_t i, diff = 0;
    char *out, *p = (char*) path;

    // check for \n that needs to be encoded
    if (memchr(p, '\n', *len) != NULL)
        enc = true;

    if (quotes) {
        // check for " that needs to be encoded
        p = (char*) path;
        do {
            p = memchr(p, '"', path + *len - p);
            if (p != NULL) {
                ++p;
                ++diff;
            }
        } while (p != NULL);
    }

    // check if an encode was needed
    if (!enc && diff == 0)
        return strdup(path);

    // allocate space for encoded path
    p = out = (char*) malloc(*len + diff);
    if (out == NULL)
        return NULL;

    // copy the path while performing encoding
    for (i = 0; i < *len; ++i) {
        if (*path == '\n') {
            // encoded \n is \0
            *p++ = 0;
        } else if (quotes && *path == '"') {
            // encoded " is ""
            *p++ = '"';
            *p++ = '"';
        } else
            *p++ = *path;
        ++path;
    }

    *len += diff;
    return out;
}

// Update free space in status bar of console
void bftps_common_update_free_space(bftps_session_context_t *session)
{
    //TODO
#if defined(_3DS) || defined(__SWITCH__)
#define KiB (1024.0)
#define MiB (1024.0*KiB)
#define GiB (1024.0*MiB)
    /*
  char           buffer[16];
  struct statvfs st;
  double         bytes_free;
  int            rc, len;

  rc = statvfs("sdmc:/", &st);
  if(rc != 0)
    console_print(RED "statvfs: %d %s\n" RESET, errno, strerror(errno));
  else
  {
    bytes_free = (double)st.f_bsize * st.f_bfree;

    if     (bytes_free < 1000.0)
      len = snprintf(buffer, sizeof(buffer), "%.0lfB", bytes_free);
    else if(bytes_free < 10.0*KiB)
      len = snprintf(buffer, sizeof(buffer), "%.2lfKiB", floor((bytes_free*100.0)/KiB)/100.0);
    else if(bytes_free < 100.0*KiB)
      len = snprintf(buffer, sizeof(buffer), "%.1lfKiB", floor((bytes_free*10.0)/KiB)/10.0);
    else if(bytes_free < 1000.0*KiB)
      len = snprintf(buffer, sizeof(buffer), "%.0lfKiB", floor(bytes_free/KiB));
    else if(bytes_free < 10.0*MiB)
      len = snprintf(buffer, sizeof(buffer), "%.2lfMiB", floor((bytes_free*100.0)/MiB)/100.0);
    else if(bytes_free < 100.0*MiB)
      len = snprintf(buffer, sizeof(buffer), "%.1lfMiB", floor((bytes_free*10.0)/MiB)/10.0);
    else if(bytes_free < 1000.0*MiB)
      len = snprintf(buffer, sizeof(buffer), "%.0lfMiB", floor(bytes_free/MiB));
    else if(bytes_free < 10.0*GiB)
      len = snprintf(buffer, sizeof(buffer), "%.2lfGiB", floor((bytes_free*100.0)/GiB)/100.0);
    else if(bytes_free < 100.0*GiB)
      len = snprintf(buffer, sizeof(buffer), "%.1lfGiB", floor((bytes_free*10.0)/GiB)/10.0);
    else
      len = snprintf(buffer, sizeof(buffer), "%.0lfGiB", floor(bytes_free/GiB));

    console_set_status("\x1b[0;%dH" GREEN "%s", 50-len, buffer);
    
  }*/
#endif
}

// change to parent directory

void bftps_common_cd_up(bftps_session_context_t *session) {
    char *slash = NULL, *p;

    // remove basename from cwd
    for (p = session->cwd; *p; ++p) {
        if (*p == '/')
            slash = p;
    }
    *slash = '\0';
    if (strlen(session->cwd) == 0)
        strcat(session->cwd, "/");
}

// validate a path

int bftps_common_validate_path(const char *args) {
    // make sure no path components are '..'
    const char *p = args;
    while ((p = strstr(p, "/..")) != NULL) {
        if (p[3] == 0 || p[3] == '/')
            return -1;
    }

    // make sure there are no '//'
    if (strstr(args, "//") != NULL)
        return -1;

    return 0;
}

// get a path relative to cwd

int bftps_common_build_path(bftps_session_context_t *session,
        const char* cwd, const char* args) {
    session->dataBufferSize = 0;
    memset(session->dataBuffer, 0, sizeof (session->dataBuffer));

    // make sure the input is a valid path
    if (0 != bftps_common_validate_path(args)) {
        return EINVAL;
    }

    if (args[0] == '/') {
        // this is an absolute path
        size_t len = strlen(args);
        if (len > sizeof (session->dataBuffer) - 1) {
            return ENAMETOOLONG;
        }

        memcpy(session->dataBuffer, args, len);
        session->dataBufferSize = len;
    } else {
        // this is a relative path
        int result = 0;
        if (strcmp(cwd, "/") == 0)
            result = snprintf(session->dataBuffer, sizeof (session->dataBuffer),
                "/%s", args);
        else
            result = snprintf(session->dataBuffer, sizeof (session->dataBuffer),
                "%s/%s", cwd, args);
        if (result >= sizeof (session->dataBuffer)) {
            return ENAMETOOLONG;
        }
        session->dataBufferSize = result;
    }

    // remove trailing /
    char *p = session->dataBuffer + session->dataBufferSize;
    // TODO shouldn't it be at most only one trailing / ?
    while (p > session->dataBuffer && *--p == '/') {
        *p = '\0';
        --session->dataBufferSize;
    }

    // if we ended with an empty path, it is the root directory
    if (session->dataBufferSize == 0)
        session->dataBuffer[session->dataBufferSize++] = '/';

    return 0;
}