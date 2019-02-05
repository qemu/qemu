/*
 * Common Option ROM Functions for C code
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright (c) 2015-2019 Red Hat Inc.
 *   Authors:
 *     Marc Marí <marc.mari.barcelo@gmail.com>
 *     Richard W.M. Jones <rjones@redhat.com>
 *     Stefano Garzarella <sgarzare@redhat.com>
 */

#ifndef OPTROM_H
#define OPTROM_H

#include <stdint.h>
#include "../../include/standard-headers/linux/qemu_fw_cfg.h"

#define barrier() asm("" : : : "memory")

#ifdef __clang__
#define ADDR32
#else
#define ADDR32 "addr32 "
#endif

static inline void outb(uint8_t value, uint16_t port)
{
    asm volatile("outb %0, %w1" : : "a"(value), "Nd"(port));
}

static inline void outw(uint16_t value, uint16_t port)
{
    asm volatile("outw %0, %w1" : : "a"(value), "Nd"(port));
}

static inline void outl(uint32_t value, uint16_t port)
{
    asm volatile("outl %0, %w1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;

    asm volatile("inb %w1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline uint16_t inw(uint16_t port)
{
    uint16_t value;

    asm volatile("inw %w1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline uint32_t inl(uint16_t port)
{
    uint32_t value;

    asm volatile("inl %w1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void insb(uint16_t port, uint8_t *buf, uint32_t len)
{
    asm volatile("rep insb %%dx, %%es:(%%edi)"
                 : "+c"(len), "+D"(buf) : "d"(port) : "memory");
}

static inline uint32_t bswap32(uint32_t x)
{
    asm("bswapl %0" : "=r" (x) : "0" (x));
    return x;
}

static inline uint64_t bswap64(uint64_t x)
{
    asm("bswapl %%eax; bswapl %%edx; xchg %%eax, %%edx" : "=A" (x) : "0" (x));
    return x;
}

static inline uint64_t cpu_to_be64(uint64_t x)
{
    return bswap64(x);
}

static inline uint32_t cpu_to_be32(uint32_t x)
{
    return bswap32(x);
}

static inline uint32_t be32_to_cpu(uint32_t x)
{
    return bswap32(x);
}

#endif /* OPTROM_H */
