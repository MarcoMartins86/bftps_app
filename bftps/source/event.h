#ifndef EVENT_H
#define EVENT_H

#ifdef __cplusplus
extern "C" {
#endif

    typedef void* event_handle_t;
    
    extern int event_create(event_handle_t* event);
    extern int event_set(event_handle_t event);
    extern int event_reset(event_handle_t event);
    extern int event_wait(event_handle_t event, int timeout_ms);
    extern int event_destroy(event_handle_t* event);


#ifdef __cplusplus
}
#endif

#endif /* EVENT_H */

