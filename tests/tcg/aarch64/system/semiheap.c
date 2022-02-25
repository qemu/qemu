/*
 * Semihosting System HEAPINFO Test
 *
 * Copyright (c) 2021 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <inttypes.h>
#include <stddef.h>
#include <minilib.h>

#define SYS_HEAPINFO    0x16

uintptr_t __semi_call(uintptr_t type, uintptr_t arg0)
{
    register uintptr_t t asm("x0") = type;
    register uintptr_t a0 asm("x1") = arg0;
    asm("hlt 0xf000"
        : "=r" (t)
        : "r" (t), "r" (a0)
        : "memory" );

    return t;
}

int main(int argc, char *argv[argc])
{
    struct {
        void *heap_base;
        void *heap_limit;
        void *stack_base;
        void *stack_limit;
    } info = { };
    void *ptr_to_info = (void *) &info;
    uint32_t *ptr_to_heap;
    int i;

    ml_printf("Semihosting Heap Info Test\n");

    __semi_call(SYS_HEAPINFO, (uintptr_t) &ptr_to_info);

    if (info.heap_base == NULL || info.heap_limit == NULL) {
        ml_printf("null heap: %p -> %p\n", info.heap_base, info.heap_limit);
        return -1;
    }

    /* Error if heap base is above limit */
    if ((uintptr_t) info.heap_base >= (uintptr_t) info.heap_limit) {
        ml_printf("heap base %p >= heap_limit %p\n",
               info.heap_base, info.heap_limit);
        return -2;
    }

    if (info.stack_base == NULL) {
        ml_printf("null stack: %p -> %p\n", info.stack_base, info.stack_limit);
        return -3;
    }

    /*
     * boot.S put our stack somewhere inside the data segment of the
     * ELF file, and we know that SYS_HEAPINFO won't pick a range
     * that overlaps with part of a loaded ELF file. So the info
     * struct (on the stack) should not be inside the reported heap.
     */
    if (ptr_to_info > info.heap_base && ptr_to_info < info.heap_limit) {
        ml_printf("info appears to be inside the heap: %p in %p:%p\n",
               ptr_to_info, info.heap_base, info.heap_limit);
        return -4;
    }

    ml_printf("heap: %p -> %p\n", info.heap_base, info.heap_limit);
    ml_printf("stack: %p <- %p\n", info.stack_limit, info.stack_base);

    /* finally can we read/write the heap */
    ptr_to_heap = (uint32_t *) info.heap_base;
    for (i = 0; i < 512; i++) {
        *ptr_to_heap++ = i;
    }
    ptr_to_heap = (uint32_t *) info.heap_base;
    for (i = 0; i < 512; i++) {
        uint32_t tmp = *ptr_to_heap;
        if (tmp != i) {
            ml_printf("unexpected value in heap: %d @ %p", tmp, ptr_to_heap);
            return -5;
        }
        ptr_to_heap++;
    }
    ml_printf("r/w to heap upto %p\n", ptr_to_heap);

    ml_printf("Passed HeapInfo checks\n");
    return 0;
}
