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

#ifndef MULTIBOOT_H
#define MULTIBOOT_H

#include "libc.h"

struct mb_info {
    uint32_t    flags;
    uint32_t    mem_lower;
    uint32_t    mem_upper;
    uint32_t    boot_device;
    uint32_t    cmdline;
    uint32_t    mods_count;
    uint32_t    mods_addr;
    char        syms[16];
    uint32_t    mmap_length;
    uint32_t    mmap_addr;
    uint32_t    drives_length;
    uint32_t    drives_addr;
    uint32_t    config_table;
    uint32_t    boot_loader_name;
    uint32_t    apm_table;
    uint32_t    vbe_control_info;
    uint32_t    vbe_mode_info;
    uint16_t    vbe_mode;
    uint16_t    vbe_interface_seg;
    uint16_t    vbe_interface_off;
    uint16_t    vbe_interface_len;
} __attribute__((packed));

struct mb_module {
    uint32_t    mod_start;
    uint32_t    mod_end;
    uint32_t    string;
    uint32_t    reserved;
} __attribute__((packed));

struct mb_mmap_entry {
    uint32_t    size;
    uint64_t    base_addr;
    uint64_t    length;
    uint32_t    type;
} __attribute__((packed));

#endif
