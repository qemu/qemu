/*
 * defines ioport related functions
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

/**************************************************************************
 * IO ports API
 */

#ifndef IOPORT_H
#define IOPORT_H

#include "qemu-common.h"
#include "exec/iorange.h"

typedef uint32_t pio_addr_t;
#define FMT_pioaddr     PRIx32

#define MAX_IOPORTS     (64 * 1024)
#define IOPORTS_MASK    (MAX_IOPORTS - 1)

/* These should really be in isa.h, but are here to make pc.h happy.  */
typedef void (IOPortWriteFunc)(void *opaque, uint32_t address, uint32_t data);
typedef uint32_t (IOPortReadFunc)(void *opaque, uint32_t address);
typedef void (IOPortDestructor)(void *opaque);

void ioport_register(IORange *iorange);
int register_ioport_read(pio_addr_t start, int length, int size,
                         IOPortReadFunc *func, void *opaque);
int register_ioport_write(pio_addr_t start, int length, int size,
                          IOPortWriteFunc *func, void *opaque);
void isa_unassign_ioport(pio_addr_t start, int length);
bool isa_is_ioport_assigned(pio_addr_t start);

void cpu_outb(pio_addr_t addr, uint8_t val);
void cpu_outw(pio_addr_t addr, uint16_t val);
void cpu_outl(pio_addr_t addr, uint32_t val);
uint8_t cpu_inb(pio_addr_t addr);
uint16_t cpu_inw(pio_addr_t addr);
uint32_t cpu_inl(pio_addr_t addr);

struct MemoryRegion;
struct MemoryRegionPortio;

typedef struct PortioList {
    const struct MemoryRegionPortio *ports;
    struct MemoryRegion *address_space;
    unsigned nr;
    struct MemoryRegion **regions;
    struct MemoryRegion **aliases;
    void *opaque;
    const char *name;
} PortioList;

void portio_list_init(PortioList *piolist,
                      const struct MemoryRegionPortio *callbacks,
                      void *opaque, const char *name);
void portio_list_destroy(PortioList *piolist);
void portio_list_add(PortioList *piolist,
                     struct MemoryRegion *address_space,
                     uint32_t addr);
void portio_list_del(PortioList *piolist);

#endif /* IOPORT_H */
