/*
 * Declarations for obsolete exec.c functions
 *
 * Copyright 2011 Red Hat, Inc. and/or its affiliates
 *
 * Authors:
 *  Avi Kivity <avi@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

/*
 * This header is for use by exec.c and memory.c ONLY.  Do not include it.
 * The functions declared here will be removed soon.
 */

#ifndef EXEC_OBSOLETE_H
#define EXEC_OBSOLETE_H

#ifndef WANT_EXEC_OBSOLETE
#error Do not include exec-obsolete.h
#endif

#ifndef CONFIG_USER_ONLY

ram_addr_t qemu_ram_alloc_from_ptr(DeviceState *dev, const char *name,
                                   ram_addr_t size, void *host,
                                   MemoryRegion *mr);
ram_addr_t qemu_ram_alloc(DeviceState *dev, const char *name, ram_addr_t size,
                          MemoryRegion *mr);
void qemu_ram_free(ram_addr_t addr);
void qemu_ram_free_from_ptr(ram_addr_t addr);

int cpu_register_io_memory(CPUReadMemoryFunc * const *mem_read,
                           CPUWriteMemoryFunc * const *mem_write,
                           void *opaque, enum device_endian endian);
void cpu_unregister_io_memory(int table_address);

void cpu_register_physical_memory_log(target_phys_addr_t start_addr,
                                      ram_addr_t size,
                                      ram_addr_t phys_offset,
                                      ram_addr_t region_offset,
                                      bool log_dirty);

static inline void cpu_register_physical_memory_offset(target_phys_addr_t start_addr,
                                                       ram_addr_t size,
                                                       ram_addr_t phys_offset,
                                                       ram_addr_t region_offset)
{
    cpu_register_physical_memory_log(start_addr, size, phys_offset,
                                     region_offset, false);
}

static inline void cpu_register_physical_memory(target_phys_addr_t start_addr,
                                                ram_addr_t size,
                                                ram_addr_t phys_offset)
{
    cpu_register_physical_memory_offset(start_addr, size, phys_offset, 0);
}

void qemu_register_coalesced_mmio(target_phys_addr_t addr, ram_addr_t size);
void qemu_unregister_coalesced_mmio(target_phys_addr_t addr, ram_addr_t size);

#endif

#endif
