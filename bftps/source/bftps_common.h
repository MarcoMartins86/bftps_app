#ifndef BFTPS_COMMON_H
#define BFTPS_COMMON_H

#include <stddef.h>

#include "bftps_session.h"
#include "bool.h"

#ifdef __cplusplus
extern "C" {
#endif

    extern void bftps_common_decode_buffer(char *path, size_t len);
    // Need to free the returned string
    extern char* bftps_common_encode_buffer(const char *path, size_t *len, bool quotes);
    extern void bftps_common_update_free_space(bftps_session_context_t *session);
    extern void bftps_common_cd_up(bftps_session_context_t *session);
    extern int bftps_common_validate_path(const char *args);
    extern int bftps_common_build_path(bftps_session_context_t *session,
        const char* cwd, const char* args);

#ifdef __cplusplus
}
#endif

#endif /* BFTPS_COMMON_H */

