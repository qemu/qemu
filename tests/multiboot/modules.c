/*
 * Copyright (c) 2015 Kevin Wolf <kwolf@redhat.com>
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
    struct mb_module *mod;
    unsigned int i;

    (void) magic;

    printf("Module list with %d entries at %x\n",
           mbi->mods_count, mbi->mods_addr);

    for (i = 0, mod = (struct mb_module*) mbi->mods_addr;
         i < mbi->mods_count;
         i++, mod++)
    {
        char buf[1024];
        unsigned int size = mod->mod_end - mod->mod_start;

        printf("[%p] Module: %x - %x (%d bytes) '%s'\n",
               mod, mod->mod_start, mod->mod_end, size, mod->string);

        /* Print test file, but remove the newline at the end */
        if (size < sizeof(buf)) {
            memcpy(buf, (void*) mod->mod_start, size);
            buf[size - 1] = '\0';
            printf("         Content: '%s'\n", buf);
        }
    }

    return 0;
}
