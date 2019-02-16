#ifndef BFTPS_COMMAND_H
#define BFTPS_COMMAND_H

#include "bftps_session.h"
#include "bool.h"

#ifdef __cplusplus
extern "C" {
#endif

    extern int bftps_command_send_response(
    bftps_session_context_t *session, int code, const char *fmt, ...);
    extern int bftps_command_send_response_buffer(
    bftps_session_context_t *session, const char * buffer, ssize_t length);
    extern int bftps_command_receive(bftps_session_context_t *session,
            int events);
    extern char* bftps_command_encode_path(const char *path, size_t *len,
            bool quotes);

#ifdef __cplusplus
}
#endif

#endif /* BFTPS_COMMAND_H */

