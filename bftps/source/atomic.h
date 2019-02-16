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

#define atomic_increase(x) __sync_fetch_and_add(x,1)
#define atomic_decrease(x) __sync_fetch_and_sub(x,1)
    
#endif



#ifdef __cplusplus
}
#endif

#endif /* ATOMIC_H */

