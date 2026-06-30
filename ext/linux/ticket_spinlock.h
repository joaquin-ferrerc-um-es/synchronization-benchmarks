/*
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* aarch64 version is based on Linux 3.13 */

#include "atomics.h"

unsigned long __attribute__((noinline)) lock_acquire (uint64_t *lock, unsigned long threadnum) {
    unsigned long depth = 0;
#if defined(__x86_64__)
asm volatile (
"   movw    $0,%[depth]\n"
"   nop\n"
"   nop\n"
"   nop\n"
"   mov    $0x20000,%%eax\n"
"   lock xadd %%eax,%[lock]\n"
"   mov    %%eax,%%edx\n"
"   mov    %%eax,%[depth]\n"
"   shr    $0x10,%%edx\n"
"   cmp    %%ax,%%dx\n"
"   jne    2f\n"
"1: nop\n"
"   jmp 4f\n"
"2: movzwl %[lock],%%eax\n"
"   mov    %%edx,%%ecx\n"
"   cmp    %%dx,%%ax\n"
"   je     1b\n"
"3: pause\n"
"   movzwl %[lock],%%eax\n"
"   cmp    %%cx,%%ax\n"
"   jne    3b\n"
"4:\n"
: [lock] "+m" (*lock), [depth] "=m" (depth)
:
: "cc", "eax", "ecx", "edx", "ax", "cx", "dx" );
    depth = (((depth >> 16) - (depth & 0xFFFF)) & 0xFFFF) >> 2;
#elif defined(__aarch64__)
    unsigned tmp, tmp2, tmp3;
asm volatile (
"   mov %w[depth], #0\n"
#if defined(USE_LSE)
"   mov %w[tmp3], #0x10000\n"
"   ldadda  %w[tmp3], %w[tmp], %[lock]\n"
"   nop\n"
"   nop\n"
#else
"1: ldaxr   %w[tmp], %[lock]\n"
"   add %w[tmp2], %w[tmp], #0x10, lsl #12\n"
"   stxr    %w[tmp3], %w[tmp2], %[lock]\n"
"   cbnz    %w[tmp3], 1b\n"
#endif
"   eor %w[tmp2], %w[tmp], %w[tmp], ror #16\n"
"   cbz %w[tmp2], 3f\n"
"   and %w[tmp3], %w[tmp], #0xFFFF\n"
"   lsr %w[depth], %w[tmp], #16\n"
"   sub %w[depth], %w[depth], %w[tmp3]\n"
"   and %w[depth], %w[depth], #0xFFFF\n"
"   sevl\n"
"2: wfe\n"
"   ldaxrh  %w[tmp3], %[lock]\n"
"   eor %w[tmp2], %w[tmp3], %w[tmp], lsr #16\n"
"   cbnz    %w[tmp2], 2b\n"
"3:\n"
: [tmp] "=&r" (tmp), [tmp2] "=&r" (tmp2),
  [tmp3] "=&r" (tmp3), [lock] "+Q" (*lock),
  [depth] "=&r" (depth)
: 
: );

#elif defined(__riscv)
    unsigned tmp, tmp2, tmp3;
    __asm__ volatile (
    "   li          %[depth], 0\n\t"
    "   li          %[tmp2], 0x10000\n\t"
    "   amoadd.w.aq %[tmp], %[tmp2], (%[lock_ptr])\n\t" // Incrementa de forma atómica el next_ticket
    "   li          t6, 0xFFFF\n\t"
    "   and         %[tmp3], %[tmp], t6\n\t"
    "   srli        %[tmp2], %[tmp], 16\n\t"
    "   li          t6, 0xFFFF\n\t"
    "   and         %[tmp2], %[tmp2], t6\n\t"
    "   beq         %[tmp2], %[tmp3], 3f\n\t"           // Si coincide inmediatamente, saltar al final
    "   sub         %[depth], %[tmp2], %[tmp3]\n\t"     // Calcular la profundidad de la cola (depth)
    "   li          t6, 0xFFFF\n\t"
    "   and         %[depth], %[depth], t6\n\t"
    "2:\n\t"
    "   pause\n\t"                                      // Mitigación de consumo energético/recursos en spin-lock
    "   lhu         %[tmp3], 0(%[lock_ptr])\n\t"        // Cargar el now_serving actual de 16-bits
    "   bne         %[tmp2], %[tmp3], 2b\n\t"           // Esperar hasta que sea nuestro turno
    "3:\n\t"
    : [tmp] "=&r" (tmp), [tmp2] "=&r" (tmp2),
      [tmp3] "=&r" (tmp3), [depth] "=&r" (depth)
    : [lock_ptr] "r" (lock)
    : "memory", "t6" );
#endif

    return depth;
}

static inline void lock_release (uint64_t *lock, unsigned long threadnum) {
#if defined(__x86_64__)
asm volatile (
"   addw    $0x2,%[lock]\n"
: [lock] "+m" (*lock)
:
: "cc" );
#elif defined(__aarch64__)
    unsigned long tmp;
asm volatile (
#if defined(USE_LSE)
"   mov %w[tmp], #1\n"
"   staddlh %w[tmp], %[lock]\n"
"   nop\n"
#else
"   ldrh    %w[tmp], %[lock]\n"
"   add %w[tmp], %w[tmp], #0x1\n"
"   stlrh   %w[tmp], %[lock]\n"
#endif
: [tmp] "=&r" (tmp), [lock] "+Q" (*lock)
:
: );

#elif defined(__riscv)
    unsigned long tmp;
    __asm__ volatile (
    "   lhu     %[tmp], 0(%[lock_ptr])\n\t"  // Carga el ticket en servicio actual (now_serving)
    "   addi    %[tmp], %[tmp], 1\n\t"       // Incrementa para dar paso al siguiente hilo
    "   fence   rw, w\n\t"                   // Barrera de liberación (Release semantics)
    "   sh      %[tmp], 0(%[lock_ptr])\n\t"  // Almacena el nuevo valor de vuelta
    : [tmp] "=&r" (tmp)
    : [lock_ptr] "r" (lock)
    : "memory" );
#endif
}

/* vim: set tabstop=8 shiftwidth=8 softtabstop=8 noexpandtab: */