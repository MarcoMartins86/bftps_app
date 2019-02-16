#ifndef BFTPS_TRANSFER_DIR_H
#define BFTPS_TRANSFER_DIR_H

#include <stddef.h>
#include <sys/stat.h>
#ifdef _3DS
#include <3ds.h>
#endif
#include "bftps_transfer.h"
#include "bool.h"
#include "macros.h"

#ifdef __cplusplus
extern "C" {
#endif

    // ftp_transfer_dir mode 
    typedef enum {
        BFTPS_TRANSFER_DIR_MODE_INVALID, /* Invalid */
        BFTPS_TRANSFER_DIR_MODE_LIST, /* Long list */
        BFTPS_TRANSFER_DIR_MODE_MLSD, /* Machine list directory */
        BFTPS_TRANSFER_DIR_MODE_MLST, /* Machine list */
        BFTPS_TRANSFER_DIR_MODE_NLST, /* Short list */
        BFTPS_TRANSFER_DIR_MODE_STAT, /* Stat command */
    } bftps_transfer_dir_mode_t;

    typedef enum {
        BFTPS_TRANSFER_DIR_MLST_TYPE = BIT(0),
        BFTPS_TRANSFER_DIR_MLST_SIZE = BIT(1),
        BFTPS_TRANSFER_DIR_MLST_MODIFY = BIT(2),
        BFTPS_TRANSFER_DIR_MLST_PERM = BIT(3),
        BFTPS_TRANSFER_DIR_MLST_UNIX_MODE = BIT(4),
    } bftps_transfer_dir_mlst_flags_t;
    
    typedef struct _bftps_session_context_t bftps_session_context_t; // prototype declaration to avoid cyclic includes
    extern int bftps_transfer_dir(bftps_session_context_t *session, const char *args,
        bftps_transfer_dir_mode_t mode, bool workaround);
    extern int bftps_transfer_dir_fill_dirent(bftps_session_context_t *session, 
        const struct stat *st,  const char *path, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* BFTPS_TRANSFER_DIR_H */

