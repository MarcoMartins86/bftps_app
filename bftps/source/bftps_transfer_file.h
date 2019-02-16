#ifndef BFTPS_TRANSFER_FILE_H
#define BFTPS_TRANSFER_FILE_H

#include "bftps_transfer.h"
#include "bftps_session.h"

#ifdef __cplusplus
extern "C" {
#endif

    // bftps_transfer_file mode

    typedef enum {
        BFTPS_TRANSFER_FILE_RETR, /* Retrieve a file */
        BFTPS_TRANSFER_FILE_STOR, /* Store a file */
        BFTPS_TRANSFER_FILE_APPE, /* Append a file */
    } bftps_transfer_file_mode_t;

    extern int bftps_transfer_file(bftps_session_context_t *session,
            const char *args, bftps_transfer_file_mode_t mode);

#ifdef __cplusplus
}
#endif

#endif /* BFTPS_TRANSFER_FILE_H */

