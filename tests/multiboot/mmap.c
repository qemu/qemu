/*
 * Copyright (c) 2013 Kevin Wolf <kwolf@redhat.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "libc.h"
#include "multiboot.h"

int test_main(uint32_t magic, struct mb_info *mbi)
{
    uintptr_t entry_addr;
    struct mb_mmap_entry *entry;

    (void) magic;

    printf("Lower memory: %dk\n", mbi->mem_lower);
    printf("Upper memory: %dk\n", mbi->mem_upper);

    printf("\ne820 memory map:\n");

    for (entry_addr = mbi->mmap_addr;
         entry_addr < mbi->mmap_addr + mbi->mmap_length;
         entry_addr += entry->size + 4)
    {
        entry = (struct mb_mmap_entry*) entry_addr;

        printf("%#llx - %#llx: type %d [entry size: %d]\n",
               entry->base_addr,
               entry->base_addr + entry->length,
               entry->type,
               entry->size);
    }

    printf("\nmmap start:       %#x\n", mbi->mmap_addr);
    printf("mmap end:         %#x\n", mbi->mmap_addr + mbi->mmap_length);
    printf("real mmap end:    %#x\n", entry_addr);

    return 0;
}
