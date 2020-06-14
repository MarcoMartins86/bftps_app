/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   atomic.h
 * Author: marco
 *
 * Created on January 26, 2019, 7:17 PM
 */

#ifndef ATOMIC_H
#define ATOMIC_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __GNUC__

    //#define atomic_increase(x) __sync_fetch_and_add(x,1)
    //#define atomic_decrease(x) __sync_fetch_and_sub(x,1)
#define atomic_compare_swap(ptr, oldval, newval) __sync_bool_compare_and_swap(ptr, oldval,newval)

    // https://attractivechaos.wordpress.com/2011/10/06/multi-threaded-programming-efficiency-of-locking/
    typedef volatile int spinlock_t;
#define spinlock_acquire(lock) while (__sync_lock_test_and_set(&lock, 1)) while (lock);
#define spinlock_release(lock) __sync_lock_release(&lock);
    
#endif



#ifdef __cplusplus
}
#endif

#endif /* ATOMIC_H */

