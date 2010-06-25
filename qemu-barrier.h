#ifndef __QEMU_BARRIER_H
#define __QEMU_BARRIER_H 1

/* FIXME: arch dependant, x86 version */
#define smp_wmb()   asm volatile("" ::: "memory")

/* Compiler barrier */
#define barrier()   asm volatile("" ::: "memory")

#endif
