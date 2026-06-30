/* * Copyright (c) 2017 ARM Limited. All rights reserved.
 * SPDX-License-Identifier:    BSD-3-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or
 * other materials provided with the distribution.
 *
 * Neither the name of ARM Limited nor the names of its contributors may be used
 * to endorse or promote products derived from this software without specific
 * prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Arm Shared Memory Synchronization Benchmark (SMS)
 * commit: 85a4b2456f1c84e2235a527d8b2b69be99621e94
 * August 6 2018
 */

#pragma once

#include "llsc.h"

#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#ifdef initialize_lock
#undef initialize_lock
#endif

#ifdef parse_test_args
#undef parse_test_args
#endif

#ifdef thread_local_init
#undef thread_local_init
#endif

#define initialize_lock(lock, pinorder, threads) clh_lock_init(lock, threads)
#define parse_test_args(args, argc, argv) clh_parse_args(args, argc, argv)
#define thread_local_init(smtid) clh_thread_local_init(smtid)


struct clh_node
{
    struct clh_node *prev;
    unsigned long wait;
} __attribute__ ((aligned (CACHE_LINE)));

struct clh_node_pointer
{
    struct clh_node *ptr;
} __attribute__ ((aligned (CACHE_LINE)));

struct clh_lock
{
    struct clh_node node;
    unsigned long num_cores;
    struct clh_node *tail __attribute__ ((aligned(CACHE_LINE)));
};

static bool without_wfe;
static struct clh_lock global_clh_lock;  // clh lock queue

static struct clh_node_pointer *clh_nodeptr;  // clh node pointer array
static struct clh_node *clh_nodepool;  // clh node struct array

/* additional parameter to enable WFE(default) or disable WFE */
static void clh_parse_args(test_args_t * unused, int argc, char** argv) {
    int i = 0;
#if defined(__aarch64__)
    without_wfe = false;
#else
    /* non-aarch64 architectures (like RISC-V) do not support ARM WFE */
    without_wfe = true;
#endif

    /* extended options retrieved after '--' operator */
    while ((i = getopt(argc, argv, "w")) != -1)
    {
        switch (i) {
          case 'w':
            without_wfe = true;
            break;

          default:
            fprintf(stderr,
                    "clh_spinlock additional options after --:\n"
                    "\t[-h print this msg]\n"
                    "\t[-w without_wfe, aarch64 default is false, non-aarch64 default is true]\n");
            exit(2);
        }
    }
}

static inline void clh_lock_init(uint64_t *u64_lock, unsigned long num_cores)
{
    /* default tail node should be set to 0 */
    global_clh_lock.node.prev = NULL;
    global_clh_lock.node.wait = 0;
    global_clh_lock.num_cores = num_cores;
    global_clh_lock.tail = &global_clh_lock.node;

    /* save clh_lock pointer to global u64int_t */
    *u64_lock = (uint64_t)&global_clh_lock;

    /* calloc will initialize all memory to zero automatically */
    if (clh_nodeptr) free(clh_nodeptr);
    clh_nodeptr = calloc(num_cores, sizeof(struct clh_node_pointer));
    if (clh_nodeptr == NULL) exit(errno);


    if (clh_nodepool) free(clh_nodepool);
    clh_nodepool = calloc(num_cores, sizeof(struct clh_node));
    if (clh_nodepool == NULL) exit(errno);

#ifdef DDEBUG
    printf("CLH: global_clh_lock=%llx\n", (long long unsigned int) &global_clh_lock);
#endif
}

static inline void clh_thread_local_init(unsigned long smtid)
{
    /* initialize clh node pointer array individually */
    clh_nodepool[smtid].wait = 1;
    clh_nodeptr[smtid].ptr = &clh_nodepool[smtid];
}

static inline void clh_lock(struct clh_lock *lock, struct clh_node *node, bool use_wfe, unsigned long tid)
{
    /* must set wait to 1 first, otherwise next node after new tail will not spin */
    node->wait = 1;
    
    /* En RISC-V esto genera automáticamente un amoswap.d.aqrl (AMO) */
    struct clh_node *prev = node->prev = __atomic_exchange_n(&lock->tail, node, __ATOMIC_ACQ_REL);
#ifdef DDEBUG
    printf("T%lu LOCK: prev<-node: %llx<-%llx\n", tid, (long long unsigned int)prev, (long long unsigned int)node);
#endif

    /* CLH spinlock: spinning on previous node's wait status */
    if (use_wfe)
    {
        if (__atomic_load_n(&prev->wait, __ATOMIC_ACQUIRE))
        {
            SEVL();
            while (WFE() && LDXR(&prev->wait, __ATOMIC_ACQUIRE))
            {
                DOZE();
            }
        }
    }
    else
    {
        while (__atomic_load_n(&prev->wait, __ATOMIC_ACQUIRE))
        {
#if defined(__riscv)
            /* Optimización para RISC-V: mitiga el consumo y la contención del pipeline */
            __asm__ __volatile__ ("pause" ::: "memory");
#else
            ;
#endif
        }
    }
}

/* return the previous node as reused node for the next clh_lock() */
static inline void clh_unlock(struct clh_node *node, unsigned long tid)
{
#ifdef DDEBUG
    printf("T%lu UNLOCK: node: %llx\n", tid, (long long unsigned int)node);
#endif
    /* CLH spinlock: release current node by resetting wait status */
#ifdef USE_DMB
    __atomic_thread_fence(__ATOMIC_RELEASE);
    __atomic_store_n(&node->wait, 0, __ATOMIC_RELAXED);
#else
    __atomic_store_n(&node->wait, 0, __ATOMIC_RELEASE);
#endif
}

/* standard lockhammer lock_acquire and lock_release interfaces */
static unsigned long __attribute__((noinline))
lock_acquire (uint64_t *lock, unsigned long threadnum)
{
    clh_lock(&global_clh_lock, clh_nodeptr[threadnum].ptr, !without_wfe, threadnum);
    return 1;
}

static inline void lock_release (uint64_t *lock, unsigned long threadnum)
{
    struct clh_node* prev = clh_nodeptr[threadnum].ptr->prev;
    clh_unlock(clh_nodeptr[threadnum].ptr, threadnum);
    clh_nodeptr[threadnum].ptr = prev;
}

/* vim: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: */