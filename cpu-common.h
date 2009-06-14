#ifndef CPU_COMMON_H
#define CPU_COMMON_H 1

/* CPU interfaces that are target indpendent.  */

#if defined(__arm__) || defined(__sparc__) || defined(__mips__) || defined(__hppa__)
#define WORDS_ALIGNED
#endif

#include "bswap.h"

/* address in the RAM (different from a physical address) */
#ifdef CONFIG_KQEMU
/* FIXME: This is wrong.  */
typedef uint32_t ram_addr_t;
#else
typedef unsigned long ram_addr_t;
#endif

/* memory API */

typedef void CPUWriteMemoryFunc(void *opaque, target_phys_addr_t addr, uint32_t value);
typedef uint32_t CPUReadMemoryFunc(void *opaque, target_phys_addr_t addr);

void cpu_register_physical_memory_offset(target_phys_addr_t start_addr,
                                         ram_addr_t size,
                                         ram_addr_t phys_offset,
                                         ram_addr_t region_offset);
static inline void cpu_register_physical_memory(target_phys_addr_t start_addr,
                                                ram_addr_t size,
                                                ram_addr_t phys_offset)
{
    cpu_register_physical_memory_offset(start_addr, size, phys_offset, 0);
}

ram_addr_t cpu_get_physical_page_desc(target_phys_addr_t addr);
ram_addr_t qemu_ram_alloc(ram_addr_t);
void qemu_ram_free(ram_addr_t addr);
/* This should only be used for ram local to a device.  */
void *qemu_get_ram_ptr(ram_addr_t addr);
/* This should not be used by devices.  */
ram_addr_t qemu_ram_addr_from_host(void *ptr);

int cpu_register_io_memory(CPUReadMemoryFunc **mem_read,
                           CPUWriteMemoryFunc **mem_write,
                           void *opaque);
void cpu_unregister_io_memory(int table_address);

void cpu_physical_memory_rw(target_phys_addr_t addr, uint8_t *buf,
                            int len, int is_write);
static inline void cpu_physical_memory_read(target_phys_addr_t addr,
                                            uint8_t *buf, int len)
{
    cpu_physical_memory_rw(addr, buf, len, 0);
}
static inline void cpu_physical_memory_write(target_phys_addr_t addr,
                                             const uint8_t *buf, int len)
{
    cpu_physical_memory_rw(addr, (uint8_t *)buf, len, 1);
}
void *cpu_physical_memory_map(target_phys_addr_t addr,
                              target_phys_addr_t *plen,
                              int is_write);
void cpu_physical_memory_unmap(void *buffer, target_phys_addr_t len,
                               int is_write, target_phys_addr_t access_len);
void *cpu_register_map_client(void *opaque, void (*callback)(void *opaque));
void cpu_unregister_map_client(void *cookie);

uint32_t ldub_phys(target_phys_addr_t addr);
uint32_t lduw_phys(target_phys_addr_t addr);
uint32_t ldl_phys(target_phys_addr_t addr);
uint64_t ldq_phys(target_phys_addr_t addr);
void stl_phys_notdirty(target_phys_addr_t addr, uint32_t val);
void stq_phys_notdirty(target_phys_addr_t addr, uint64_t val);
void stb_phys(target_phys_addr_t addr, uint32_t val);
void stw_phys(target_phys_addr_t addr, uint32_t val);
void stl_phys(target_phys_addr_t addr, uint32_t val);
void stq_phys(target_phys_addr_t addr, uint64_t val);

void cpu_physical_memory_write_rom(target_phys_addr_t addr,
                                   const uint8_t *buf, int len);

#define IO_MEM_SHIFT       3

#define IO_MEM_RAM         (0 << IO_MEM_SHIFT) /* hardcoded offset */
#define IO_MEM_ROM         (1 << IO_MEM_SHIFT) /* hardcoded offset */
#define IO_MEM_UNASSIGNED  (2 << IO_MEM_SHIFT)
#define IO_MEM_NOTDIRTY    (3 << IO_MEM_SHIFT)

/* Acts like a ROM when read and like a device when written.  */
#define IO_MEM_ROMD        (1)
#define IO_MEM_SUBPAGE     (2)
#define IO_MEM_SUBWIDTH    (4)

#endif /* !CPU_COMMON_H */
