/*
 * Copyright (c) 2017-2025, The Linux Foundation. All rights reserved.
 *
 * SPDX-License-Identifier:    BSD-3-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following
 * disclaimer in the documentation and/or other materials provided
 * with the distribution.
 * * Neither the name of The Linux Foundation nor the names of its
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdint.h>
#include <stdbool.h>

#ifndef __LH_ATOMICS_H_
#define __LH_ATOMICS_H_


// wait64 - wait until the 64-bit lock value equals to val
static inline void wait64 (unsigned long *lock, unsigned long val) {
#if defined(__aarch64__)
    unsigned long tmp;

    asm volatile(
            "   sevl\n"
            "1: wfe\n"
            "   ldaxr %[tmp], %[lock]\n"
            "   eor   %[tmp], %[tmp], %[val]\n"
            "   cbnz  %[tmp], 1b\n"
            : [tmp] "=&r" (tmp)
            : [lock] "Q" (*lock), [val] "r" (val)
            : );
#elif defined(__riscv)
    volatile unsigned long *v = lock;
    while (*v != val) {
        asm volatile("pause" ::: "memory");
    }
#else
    volatile unsigned long *v = lock;

    while (*v != val);
#endif
}


// wait32 - wait until the 32-bit lock value equals to val
static inline void wait32 (uint32_t *lock, uint32_t val) {
#if defined(__aarch64__)
    uint32_t tmp;

    asm volatile(
            "   sevl\n"
            "1: wfe\n"
            "   ldaxr %w[tmp], %[lock]\n"
            "   eor   %w[tmp], %w[tmp], %w[val]\n"
            "   cbnz  %w[tmp], 1b\n"
            : [tmp] "=&r" (tmp)
            : [lock] "Q" (*lock), [val] "r" (val)
            : );
#elif defined(__riscv)
    volatile uint32_t *v = lock;
    while (*v != val) {
        asm volatile("pause" ::: "memory");
    }
#else
    volatile uint32_t *v = lock;

    while (*v != val);
#endif
}

// prefetch64 - prefetch for store to L1
static inline void prefetch64 (unsigned long *ptr) {
#if defined(__aarch64__)
    asm volatile(
            "   prfm  pstl1keep, %[ptr]\n"
            :
            : [ptr] "Q" (*(unsigned long *)ptr));
#elif defined(__riscv)
    (void)ptr;
#endif
}


// fetchadd64_acquire_release - atomically add to *ptr with acquire and release semantics; return the value of *ptr from before the add
static inline unsigned long fetchadd64_acquire_release (unsigned long *ptr, unsigned long addend) {
    unsigned long old;

#if defined(__x86_64__) && !defined(USE_BUILTIN)
    asm volatile ("lock xaddq %[val], %[ptr]"
            : [val] "=&r" (old), [ptr] "+m" (*ptr)
            : "[val]" (addend)
            : "memory", "cc");
#elif defined(__aarch64__) && !defined(USE_BUILTIN) && defined(USE_LSE)
    asm volatile ("ldaddal %[val], %[old], %[ptr]"
            : [old] "=r" (old), [ptr] "+Q" (*ptr)
            : [val] "r" (addend)
            : "memory");
#elif defined(__aarch64__) && !defined(USE_BUILTIN) && !defined(USE_LSE)
    unsigned long newval;
    unsigned int tmp;

    asm volatile ("1: ldaxr %[old], %[ptr]\n"
                  "   add   %[newval], %[old], %[val]\n"
                  "   stlxr %w[tmp], %[newval], %[ptr]\n"
                  "   cbnz  %w[tmp], 1b"
            : [tmp] "=&r" (tmp), [old] "=&r" (old), [newval] "=&r" (newval), [ptr] "+Q" (*ptr)
            : [val] "r" (addend)
            : "memory");
#elif defined(__riscv) && !defined(USE_BUILTIN)
    /* AMO puro */
    asm volatile ("amoadd.d.aqrl %[old], %[val], (%[ptr])"
            : [old] "=r" (old)
            : [val] "r" (addend), [ptr] "r" (ptr)
            : "memory");
#else
    old = __atomic_fetch_add(ptr, addend, __ATOMIC_ACQ_REL);
#endif

    return old;
}


// fetchadd64_acquire - atomically add to *ptr with acquire semantics; return the value of *ptr from before the add
static inline unsigned long fetchadd64_acquire (unsigned long *ptr, unsigned long addend) {
    unsigned long old;

#if defined(__x86_64__) && !defined(USE_BUILTIN)
    asm volatile ("lock xaddq %[val], %[ptr]"
            : [val] "=&r" (old), [ptr] "+m" (*ptr)
            : "[val]" (addend)
            : "memory", "cc");
#elif defined(__aarch64__) && !defined(USE_BUILTIN) && defined(USE_LSE)
    asm volatile ("ldadda %[val], %[old], %[ptr]"
            : [old] "=r" (old), [ptr] "+Q" (*ptr)
            : [val] "r" (addend)
            : "memory");
#elif defined(__aarch64__) && !defined(USE_BUILTIN) && !defined(USE_LSE)
    unsigned long newval;
    unsigned int tmp;

    asm volatile ("1: ldaxr %[old], %[ptr]\n"
                  "   add   %[newval], %[old], %[val]\n"
                  "   stxr  %w[tmp], %[newval], %[ptr]\n"
                  "   cbnz  %w[tmp], 1b"
            : [tmp] "=&r" (tmp), [old] "=&r" (old), [newval] "=&r" (newval), [ptr] "+Q" (*ptr)
            : [val] "r" (addend)
            : "memory");
#elif defined(__riscv) && !defined(USE_BUILTIN)
    /* AMO puro */
    asm volatile ("amoadd.d.aq %[old], %[val], (%[ptr])"
            : [old] "=r" (old)
            : [val] "r" (addend), [ptr] "r" (ptr)
            : "memory");
#else
    old = __atomic_fetch_add(ptr, addend, __ATOMIC_ACQUIRE);
#endif

    return old;
}


// fetchadd64_release - atomically add to *ptr with release semantics; return the value of *ptr from before the add
static inline unsigned long fetchadd64_release (unsigned long *ptr, unsigned long addend) {
    unsigned long old;

#if defined(__x86_64__) && !defined(USE_BUILTIN)
    asm volatile ("lock xaddq %[val], %[ptr]"
            : [val] "=&r" (old), [ptr] "+m" (*ptr)
            : "[val]" (addend)
            : "memory", "cc");
#elif defined(__aarch64__) && !defined(USE_BUILTIN) && defined(USE_LSE)
    asm volatile ("ldaddl %[val], %[old], %[ptr]"
            : [old] "=r" (old), [ptr] "+Q" (*ptr)
            : [val] "r" (addend)
            : "memory");
#elif defined(__aarch64__) && !defined(USE_BUILTIN) && !defined(USE_LSE)
    unsigned long newval;
    unsigned int tmp;

    asm volatile ("1: ldxr  %[old], %[ptr]\n"
                  "   add   %[newval], %[old], %[val]\n"
                  "   stlxr %w[tmp], %[newval], %[ptr]\n"
                  "   cbnz  %w[tmp], 1b"
            : [tmp] "=&r" (tmp), [old] "=&r" (old), [newval] "=&r" (newval), [ptr] "+Q" (*ptr)
            : [val] "r" (addend)
            : "memory");
#elif defined(__riscv) && !defined(USE_BUILTIN)
    /* AMO puro */
    asm volatile ("amoadd.d.rl %[old], %[val], (%[ptr])"
            : [old] "=r" (old)
            : [val] "r" (addend), [ptr] "r" (ptr)
            : "memory");
#else
    old = __atomic_fetch_add(ptr, addend, __ATOMIC_RELEASE);
#endif

    return old;
}


// fetchadd64 - atomically add to *ptr with acquire and release semantics; return the value of *ptr from before the add
static inline unsigned long fetchadd64 (unsigned long *ptr, unsigned long addend) {
    unsigned long old;

#if defined(__x86_64__) && !defined(USE_BUILTIN)
    asm volatile ("lock xaddq %[val], %[ptr]"
            : [val] "=&r" (old), [ptr] "+m" (*ptr)
            : "[val]" (addend)
            : "memory", "cc");
#elif defined(__aarch64__) && !defined(USE_BUILTIN) && defined(USE_LSE)
    asm volatile ("ldadd %[val], %[old], %[ptr]"
            : [old] "=r" (old), [ptr] "+Q" (*ptr)
            : [val] "r" (addend)
            : "memory");
#elif defined(__aarch64__) && !defined(USE_BUILTIN) && !defined(USE_LSE)
    unsigned long newval;
    unsigned int tmp;

    asm volatile ("1: ldxr  %[old], %[ptr]\n"
                  "   add   %[newval], %[old], %[val]\n"
                  "   stxr  %w[tmp], %[newval], %[ptr]\n"
                  "   cbnz  %w[tmp], 1b"
            : [tmp] "=&r" (tmp), [old] "=&r" (old), [newval] "=&r" (newval), [ptr] "+Q" (*ptr)
            : [val] "r" (addend)
            : "memory");
#elif defined(__riscv) && !defined(USE_BUILTIN)
    /* AMO puro */
    asm volatile ("amoadd.d %[old], %[val], (%[ptr])"
            : [old] "=r" (old)
            : [val] "r" (addend), [ptr] "r" (ptr)
            : "memory");
#else
    old = __atomic_fetch_add(ptr, addend, __ATOMIC_RELAXED);
#endif

    return old;
}



// fetchsub64 - atomically subtract val from *ptr; return the value of *ptr from before the subtraction
static inline unsigned long fetchsub64 (unsigned long *ptr, unsigned long addend) {
    unsigned long old;

#if defined(__x86_64__) && !defined(USE_BUILTIN)
    addend = (unsigned long) (-(long) addend);
    asm volatile ("lock xaddq %[val], %[ptr]"
            : [val] "=&r" (old), [ptr] "+m" (*ptr)
            : "[val]" (addend)
            : "memory", "cc");
#elif defined(__aarch64__) && !defined(USE_BUILTIN) && defined(USE_LSE)
    addend = (unsigned long) (-(long) addend);
    asm volatile ("ldadd %[val], %[old], %[ptr]"
            : [old] "=r" (old), [ptr] "+Q" (*ptr)
            : [val] "r" (addend)
            : "memory");
#elif defined(__aarch64__) && !defined(USE_BUILTIN) && !defined(USE_LSE)
    unsigned long newval;
    unsigned int tmp;

    asm volatile ("1: ldxr  %[old], %[ptr]\n"
                  "   sub   %[newval], %[old], %[val]\n"
                  "   stxr  %w[tmp], %[newval], %[ptr]\n"
                  "   cbnz  %w[tmp], 1b"
            : [tmp] "=&r" (tmp), [old] "=&r" (old), [newval] "=&r" (newval), [ptr] "+Q" (*ptr)
            : [val] "r" (addend)
            : "memory");
#elif defined(__riscv) && !defined(USE_BUILTIN)
    /* AMO puro */
    unsigned long neg = (unsigned long)(-(long)addend);
    asm volatile ("amoadd.d %[old], %[val], (%[ptr])"
            : [old] "=r" (old)
            : [val] "r" (neg), [ptr] "r" (ptr)
            : "memory");
#else
    old = __atomic_fetch_sub(ptr, addend, __ATOMIC_RELAXED);
#endif

    return old;
}


// swap64 - atomically swap val for *ptr and return the value that was at ptr before the swap
static inline unsigned long swap64 (unsigned long *ptr, unsigned long val) {
    unsigned long old;

#if defined(__x86_64__) && !defined(USE_BUILTIN)
    asm volatile ("xchgq %[val], %[ptr]"
            : [val] "=&r" (old), [ptr] "+m" (*ptr)
            : "[val]" (val)
            : "memory", "cc");
#elif defined(__aarch64__) && !defined(USE_BUILTIN) && defined(USE_LSE)
    asm volatile ("swpal %[val], %[old], %[ptr]"
            : [old] "=r" (old), [ptr] "+Q" (*ptr)
            : [val] "r" (val)
            : "memory");
#elif defined(__aarch64__) && !defined(USE_BUILTIN) && !defined(USE_LSE)
    unsigned int tmp;

    asm volatile ("1: ldaxr %[old], %[ptr]\n"
                  "   stlxr %w[tmp], %[val], %[ptr]\n"
                  "   cbnz  %w[tmp], 1b\n"
            : [tmp] "=&r" (tmp), [old] "=&r" (old), [ptr] "+Q" (*ptr)
            : [val] "r" (val)
            : "memory");
#elif defined(__riscv) && !defined(USE_BUILTIN)
    /* AMO puro */
    asm volatile ("amoswap.d.aqrl %[old], %[val], (%[ptr])"
            : [old] "=r" (old)
            : [val] "r" (val), [ptr] "r" (ptr)
            : "memory");
#else
    old = __atomic_exchange_n(ptr, val, __ATOMIC_ACQ_REL);
#endif

    return old;
}


// cas64 - if *ptr == expected, then *ptr = newval; return the value of *ptr before any update
static inline unsigned long cas64 (unsigned long *ptr, unsigned long newval, unsigned long expected) {
    unsigned long old;

#if defined(__x86_64__) && !defined(USE_BUILTIN)
    asm volatile ("lock cmpxchgq %[newval], %[ptr]"
            : [exp] "=a" (old), [ptr] "+m" (*ptr)
            : [newval] "r" (newval), "[exp]" (expected)
            : "memory", "cc");
#elif defined(__aarch64__) && !defined(USE_BUILTIN) && defined(USE_LSE)
    asm volatile ("cas %[exp], %[newval], %[ptr]"
            : [exp] "=&r" (old), [ptr] "+Q" (*ptr)
            : "[exp]" (expected), [newval] "r" (newval)
            : "memory");
#elif defined(__aarch64__) && !defined(USE_BUILTIN) && !defined(USE_LSE)
    unsigned long tmp;

    asm volatile("1: ldxr  %[old], %[ptr]\n"
                 "   eor   %[tmp], %[old], %[exp]\n"
                 "   cbnz  %[tmp], 2f\n"
                 "   stxr  %w[tmp], %[val], %[ptr]\n"
                 "   cbnz  %w[tmp], 1b\n"
                 "2:"
            : [tmp] "=&r" (tmp), [old] "=&r" (old), [ptr] "+Q" (*ptr)
            : [exp] "r" (expected), [val] "r" (newval)
            : "memory");
#elif defined(__riscv) && !defined(USE_BUILTIN)
#if defined(__riscv_zacas)
    /* AMO puro (Soportado si se compila con extensión Zacas) */
    asm volatile("amocas.d %[exp], %[val], (%[ptr])"
            : [exp] "+r" (expected)
            : [val] "r" (newval), [ptr] "r" (ptr)
            : "memory");
    old = expected;
#define RISCV_CAS_DONE
#endif
#ifndef RISCV_CAS_DONE
    /* Fallback por hardware si no hay Zacas (requiere LR/SC) */
    unsigned long tmp;
    asm volatile("1: lr.d    %[old], (%[ptr])\n"
                 "   bne     %[old], %[exp], 2f\n"
                 "   sc.d    %[tmp], %[val], (%[ptr])\n"
                 "   bnez    %[tmp], 1b\n"
                 "2:"
            : [tmp] "=&r" (tmp), [old] "=&r" (old)
            : [ptr] "r" (ptr), [exp] "r" (expected), [val] "r" (newval)
            : "memory");
#endif
#undef RISCV_CAS_DONE
#else
    old = expected;
    __atomic_compare_exchange_n(ptr, &old, expected, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
#endif

    return old;
}


// cas64_acquire - if *ptr == expected, then *ptr = newval; return the value of *ptr before any update
static inline unsigned long cas64_acquire (unsigned long *ptr, unsigned long val, unsigned long exp) {
    unsigned long old;

#if defined(__x86_64__) && !defined(USE_BUILTIN)
    asm volatile ("lock cmpxchgq %[newval], %[ptr]"
            : [exp] "=a" (old), [ptr] "+m" (*ptr)
            : [newval] "r" (val), "[exp]" (exp)
            : "memory", "cc");
#elif defined(__aarch64__) && !defined(USE_BUILTIN) && defined(USE_LSE)
    asm volatile ("casa %[exp], %[newval], %[ptr]"
            : [exp] "=&r" (old), [ptr] "+Q" (*ptr)
            : "[exp]" (exp), [newval] "r" (val)
            : "memory");
#elif defined(__aarch64__) && !defined(USE_BUILTIN) && !defined(USE_LSE)
    unsigned long tmp;

    asm volatile("1: ldaxr %[old], %[ptr]\n"
                 "   eor   %[tmp], %[old], %[exp]\n"
                 "   cbnz  %[tmp], 2f\n"
                 "   stxr  %w[tmp], %[val], %[ptr]\n"
                 "   cbnz  %w[tmp], 1b\n"
                 "2:"
            : [tmp] "=&r" (tmp), [old] "=&r" (old), [ptr] "+Q" (*ptr)
            : [exp] "r" (exp), [val] "r" (val)
            : "memory");
#elif defined(__riscv) && !defined(USE_BUILTIN)
#if defined(__riscv_zacas)
    /* AMO puro (Soportado si se compila con extensión Zacas) */
    asm volatile("amocas.d.aq %[exp], %[val], (%[ptr])"
            : [exp] "+r" (exp)
            : [val] "r" (val), [ptr] "r" (ptr)
            : "memory");
    old = exp;
#define RISCV_CAS_DONE
#endif
#ifndef RISCV_CAS_DONE
    /* Fallback por hardware si no hay Zacas (requiere LR/SC) */
    unsigned long tmp;
    asm volatile("1: lr.d.aq %[old], (%[ptr])\n"
                 "   bne     %[old], %[exp], 2f\n"
                 "   sc.d    %[tmp], %[val], (%[ptr])\n"
                 "   bnez    %[tmp], 1b\n"
                 "2:"
            : [tmp] "=&r" (tmp), [old] "=&r" (old)
            : [ptr] "r" (ptr), [exp] "r" (exp), [val] "r" (val)
            : "memory");
#endif
#undef RISCV_CAS_DONE
#else
    old = exp;
    __atomic_compare_exchange_n(ptr, &old, val, true, __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE);
#endif

    return old;
}


// cas64_release -- this is not currently being used
static inline unsigned long cas64_release (unsigned long *ptr, unsigned long val, unsigned long exp) {
    unsigned long old;

#if defined(__x86_64__) && !defined(USE_BUILTIN)
    asm volatile ("lock cmpxchgq %[newval], %[ptr]"
            : [exp] "=a" (old), [ptr] "+m" (*ptr)
            : [newval] "r" (val), "[exp]" (exp)
            : "memory", "cc");
#elif defined(__aarch64__) && !defined(USE_BUILTIN) && defined(USE_LSE)
    asm volatile ("casl %[exp], %[newval], %[ptr]"
            : [exp] "=&r" (old), [ptr] "+Q" (*ptr)
            : "[exp]" (exp), [newval] "r" (val)
            : "memory");
#elif defined(__aarch64__) && !defined(USE_BUILTIN) && !defined(USE_LSE)
    unsigned long tmp;

    asm volatile("1: ldxr  %[old], %[ptr]\n"
                 "   eor   %[tmp], %[old], %[exp]\n"
                 "   cbnz  %[tmp], 2f\n"
                 "   stlxr %w[tmp], %[val], %[ptr]\n"
                 "   cbnz  %w[tmp], 1b\n"
                 "2:"
            : [tmp] "=&r" (tmp), [old] "=&r" (old), [ptr] "+Q" (*ptr)
            : [exp] "r" (exp), [val] "r" (val)
            : "memory");
#elif defined(__riscv) && !defined(USE_BUILTIN)
#if defined(__riscv_zacas)
    /* AMO puro (Soportado si se compila con extensión Zacas) */
    asm volatile("amocas.d.rl %[exp], %[val], (%[ptr])"
            : [exp] "+r" (exp)
            : [val] "r" (val), [ptr] "r" (ptr)
            : "memory");
    old = exp;
#define RISCV_CAS_DONE
#endif
#ifndef RISCV_CAS_DONE
    /* Fallback por hardware si no hay Zacas (requiere LR/SC) */
    unsigned long tmp;
    asm volatile("1: lr.d    %[old], (%[ptr])\n"
                 "   bne     %[old], %[exp], 2f\n"
                 "   sc.d.rl %[tmp], %[val], (%[ptr])\n"
                 "   bnez    %[tmp], 1b\n"
                 "2:"
            : [tmp] "=&r" (tmp), [old] "=&r" (old)
            : [ptr] "r" (ptr), [exp] "r" (exp), [val] "r" (val)
            : "memory");
#endif
#undef RISCV_CAS_DONE
#else
    old = exp;
    __atomic_compare_exchange_n(ptr, &old, val, true, __ATOMIC_RELEASE, __ATOMIC_RELAXED);
#endif

    return old;
}


// cas64_acquire_release -- used in __TBB_machine_cmpswp8
static inline unsigned long cas64_acquire_release (unsigned long *ptr, unsigned long val, unsigned long exp) {
    unsigned long old;

#if defined(__x86_64__) && !defined(USE_BUILTIN)
    asm volatile ("lock cmpxchgq %[newval], %[ptr]"
            : [exp] "=a" (old), [ptr] "+m" (*ptr)
            : [newval] "r" (val), "[exp]" (exp)
            : "memory", "cc");
#elif defined(__aarch64__) && !defined(USE_BUILTIN) && defined(USE_LSE)
    asm volatile ("casal %[exp], %[newval], %[ptr]"
            : [exp] "=&r" (old), [ptr] "+Q" (*ptr)
            : "[exp]" (exp), [newval] "r" (val)
            : "memory");
#elif defined(__aarch64__) && !defined(USE_BUILTIN) && !defined(USE_LSE)
    unsigned long tmp;

    asm volatile("1: ldaxr %[old], %[ptr]\n"
                 "   eor   %[tmp], %[old], %[exp]\n"
                 "   cbnz  %[tmp], 2f\n"
                 "   stlxr %w[tmp], %[val], %[ptr]\n"
                 "   cbnz  %w[tmp], 1b\n"
                 "2:"
            : [tmp] "=&r" (tmp), [old] "=&r" (old), [ptr] "+Q" (*ptr)
            : [exp] "r" (exp), [val] "r" (val)
            : "memory");
#elif defined(__riscv) && !defined(USE_BUILTIN)
#if defined(__riscv_zacas)
    /* AMO puro (Soportado si se compila con extensión Zacas) */
    asm volatile("amocas.d.aqrl %[exp], %[val], (%[ptr])"
            : [exp] "+r" (exp)
            : [val] "r" (val), [ptr] "r" (ptr)
            : "memory");
    old = exp;
#define RISCV_CAS_DONE
#endif
#ifndef RISCV_CAS_DONE
    /* Fallback por hardware si no hay Zacas (requiere LR/SC) */
    unsigned long tmp;
    asm volatile("1: lr.d.aq %[old], (%[ptr])\n"
                 "   bne     %[old], %[exp], 2f\n"
                 "   sc.d.rl %[tmp], %[val], (%[ptr])\n"
                 "   bnez    %[tmp], 1b\n"
                 "2:"
            : [tmp] "=&r" (tmp), [old] "=&r" (old)
            : [ptr] "r" (ptr), [exp] "r" (exp), [val] "r" (val)
            : "memory");
#endif
#undef RISCV_CAS_DONE
#else
    old = exp;
    __atomic_compare_exchange_n(ptr, &old, val, true, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED);
#endif

    return old;
}

#endif // __LH_ATOMICS_H_

/* vim: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: */