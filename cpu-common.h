#ifndef CPU_COMMON_H
#define CPU_COMMON_H 1

/* CPU interfaces that are target indpendent.  */

#if defined(__arm__) || defined(__sparc__) || defined(__mips__) || defined(__hppa__)
#define WORDS_ALIGNED
#endif

#include "bswap.h"

/* address in the RAM (different from a physical address) */
typedef unsigned long a_ram_addr;

/* memory API */

typedef void CPUWriteMemoryFunc(void *opaque, a_target_phys_addr addr, uint32_t value);
typedef uint32_t CPUReadMemoryFunc(void *opaque, a_target_phys_addr addr);

void cpu_register_physical_memory_offset(a_target_phys_addr start_addr,
                                         a_ram_addr size,
                                         a_ram_addr phys_offset,
                                         a_ram_addr region_offset);
static inline void cpu_register_physical_memory(a_target_phys_addr start_addr,
                                                a_ram_addr size,
                                                a_ram_addr phys_offset)
{
    cpu_register_physical_memory_offset(start_addr, size, phys_offset, 0);
}

a_ram_addr cpu_get_physical_page_desc(a_target_phys_addr addr);
a_ram_addr qemu_ram_alloc(a_ram_addr);
void qemu_ram_free(a_ram_addr addr);
/* This should only be used for ram local to a device.  */
void *qemu_get_ram_ptr(a_ram_addr addr);
/* This should not be used by devices.  */
a_ram_addr qemu_ram_addr_from_host(void *ptr);

int cpu_register_io_memory(CPUReadMemoryFunc * const *mem_read,
                           CPUWriteMemoryFunc * const *mem_write,
                           void *opaque);
void cpu_unregister_io_memory(int table_address);

void cpu_physical_memory_rw(a_target_phys_addr addr, uint8_t *buf,
                            int len, int is_write);
static inline void cpu_physical_memory_read(a_target_phys_addr addr,
                                            uint8_t *buf, int len)
{
    cpu_physical_memory_rw(addr, buf, len, 0);
}
static inline void cpu_physical_memory_write(a_target_phys_addr addr,
                                             const uint8_t *buf, int len)
{
    cpu_physical_memory_rw(addr, (uint8_t *)buf, len, 1);
}
void *cpu_physical_memory_map(a_target_phys_addr addr,
                              a_target_phys_addr *plen,
                              int is_write);
void cpu_physical_memory_unmap(void *buffer, a_target_phys_addr len,
                               int is_write, a_target_phys_addr access_len);
void *cpu_register_map_client(void *opaque, void (*callback)(void *opaque));
void cpu_unregister_map_client(void *cookie);

uint32_t ldub_phys(a_target_phys_addr addr);
uint32_t lduw_phys(a_target_phys_addr addr);
uint32_t ldl_phys(a_target_phys_addr addr);
uint64_t ldq_phys(a_target_phys_addr addr);
void stl_phys_notdirty(a_target_phys_addr addr, uint32_t val);
void stq_phys_notdirty(a_target_phys_addr addr, uint64_t val);
void stb_phys(a_target_phys_addr addr, uint32_t val);
void stw_phys(a_target_phys_addr addr, uint32_t val);
void stl_phys(a_target_phys_addr addr, uint32_t val);
void stq_phys(a_target_phys_addr addr, uint64_t val);

void cpu_physical_memory_write_rom(a_target_phys_addr addr,
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
