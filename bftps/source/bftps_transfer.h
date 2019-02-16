
#ifndef BFTPS_TRANSFER_H
#define BFTPS_TRANSFER_H

#ifdef __cplusplus
extern "C" {
#endif

    // Loop status 
    typedef enum {
        BFTPS_TRANSFER_LOOP_STATUS_INVALID, /* Invalid */
        BFTPS_TRANSFER_LOOP_STATUS_CONTINUE, /* Continue looping */
        BFTPS_TRANSFER_LOOP_STATUS_RESTART, /* Reinitialize */
        BFTPS_TRANSFER_LOOP_STATUS_EXIT, /* Terminate looping */
    } bftps_transfer_loop_status_t;


#ifdef __cplusplus
}
#endif

#endif /* BFTPS_TRANSFER_H */

