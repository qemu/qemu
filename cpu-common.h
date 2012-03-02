#ifndef CPU_COMMON_H
#define CPU_COMMON_H 1

/* CPU interfaces that are target independent.  */

#ifdef TARGET_PHYS_ADDR_BITS
#include "targphys.h"
#endif

#ifndef NEED_CPU_H
#include "poison.h"
#endif

#include "bswap.h"
#include "qemu-queue.h"

#if !defined(CONFIG_USER_ONLY)

enum device_endian {
    DEVICE_NATIVE_ENDIAN,
    DEVICE_BIG_ENDIAN,
    DEVICE_LITTLE_ENDIAN,
};

/* address in the RAM (different from a physical address) */
#if defined(CONFIG_XEN_BACKEND) && TARGET_PHYS_ADDR_BITS == 64
typedef uint64_t ram_addr_t;
#  define RAM_ADDR_MAX UINT64_MAX
#  define RAM_ADDR_FMT "%" PRIx64
#else
typedef uintptr_t ram_addr_t;
#  define RAM_ADDR_MAX UINTPTR_MAX
#  define RAM_ADDR_FMT "%" PRIxPTR
#endif

/* memory API */

typedef void CPUWriteMemoryFunc(void *opaque, target_phys_addr_t addr, uint32_t value);
typedef uint32_t CPUReadMemoryFunc(void *opaque, target_phys_addr_t addr);

void qemu_ram_remap(ram_addr_t addr, ram_addr_t length);
/* This should only be used for ram local to a device.  */
void *qemu_get_ram_ptr(ram_addr_t addr);
void *qemu_ram_ptr_length(ram_addr_t addr, ram_addr_t *size);
/* Same but slower, to use for migration, where the order of
 * RAMBlocks must not change. */
void *qemu_safe_ram_ptr(ram_addr_t addr);
void qemu_put_ram_ptr(void *addr);
/* This should not be used by devices.  */
int qemu_ram_addr_from_host(void *ptr, ram_addr_t *ram_addr);
ram_addr_t qemu_ram_addr_from_host_nofail(void *ptr);
void qemu_ram_set_idstr(ram_addr_t addr, const char *name, DeviceState *dev);

void cpu_physical_memory_rw(target_phys_addr_t addr, uint8_t *buf,
                            int len, int is_write);
static inline void cpu_physical_memory_read(target_phys_addr_t addr,
                                            void *buf, int len)
{
    cpu_physical_memory_rw(addr, buf, len, 0);
}
static inline void cpu_physical_memory_write(target_phys_addr_t addr,
                                             const void *buf, int len)
{
    cpu_physical_memory_rw(addr, (void *)buf, len, 1);
}
void *cpu_physical_memory_map(target_phys_addr_t addr,
                              target_phys_addr_t *plen,
                              int is_write);
void cpu_physical_memory_unmap(void *buffer, target_phys_addr_t len,
                               int is_write, target_phys_addr_t access_len);
void *cpu_register_map_client(void *opaque, void (*callback)(void *opaque));
void cpu_unregister_map_client(void *cookie);

/* Coalesced MMIO regions are areas where write operations can be reordered.
 * This usually implies that write operations are side-effect free.  This allows
 * batching which can make a major impact on performance when using
 * virtualization.
 */
void qemu_flush_coalesced_mmio_buffer(void);

uint32_t ldub_phys(target_phys_addr_t addr);
uint32_t lduw_le_phys(target_phys_addr_t addr);
uint32_t lduw_be_phys(target_phys_addr_t addr);
uint32_t ldl_le_phys(target_phys_addr_t addr);
uint32_t ldl_be_phys(target_phys_addr_t addr);
uint64_t ldq_le_phys(target_phys_addr_t addr);
uint64_t ldq_be_phys(target_phys_addr_t addr);
void stb_phys(target_phys_addr_t addr, uint32_t val);
void stw_le_phys(target_phys_addr_t addr, uint32_t val);
void stw_be_phys(target_phys_addr_t addr, uint32_t val);
void stl_le_phys(target_phys_addr_t addr, uint32_t val);
void stl_be_phys(target_phys_addr_t addr, uint32_t val);
void stq_le_phys(target_phys_addr_t addr, uint64_t val);
void stq_be_phys(target_phys_addr_t addr, uint64_t val);

#ifdef NEED_CPU_H
uint32_t lduw_phys(target_phys_addr_t addr);
uint32_t ldl_phys(target_phys_addr_t addr);
uint64_t ldq_phys(target_phys_addr_t addr);
void stl_phys_notdirty(target_phys_addr_t addr, uint32_t val);
void stq_phys_notdirty(target_phys_addr_t addr, uint64_t val);
void stw_phys(target_phys_addr_t addr, uint32_t val);
void stl_phys(target_phys_addr_t addr, uint32_t val);
void stq_phys(target_phys_addr_t addr, uint64_t val);
#endif

void cpu_physical_memory_write_rom(target_phys_addr_t addr,
                                   const uint8_t *buf, int len);

extern struct MemoryRegion io_mem_ram;
extern struct MemoryRegion io_mem_rom;
extern struct MemoryRegion io_mem_unassigned;
extern struct MemoryRegion io_mem_notdirty;

#endif

#endif /* !CPU_COMMON_H */
