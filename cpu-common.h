#ifndef CPU_COMMON_H
#define CPU_COMMON_H 1

/* CPU interfaces that are target independent.  */

#include "hwaddr.h"

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
#if defined(CONFIG_XEN_BACKEND)
typedef uint64_t ram_addr_t;
#  define RAM_ADDR_MAX UINT64_MAX
#  define RAM_ADDR_FMT "%" PRIx64
#else
typedef uintptr_t ram_addr_t;
#  define RAM_ADDR_MAX UINTPTR_MAX
#  define RAM_ADDR_FMT "%" PRIxPTR
#endif

/* memory API */

typedef void CPUWriteMemoryFunc(void *opaque, hwaddr addr, uint32_t value);
typedef uint32_t CPUReadMemoryFunc(void *opaque, hwaddr addr);

void qemu_ram_remap(ram_addr_t addr, ram_addr_t length);
/* This should only be used for ram local to a device.  */
void *qemu_get_ram_ptr(ram_addr_t addr);
void qemu_put_ram_ptr(void *addr);
/* This should not be used by devices.  */
int qemu_ram_addr_from_host(void *ptr, ram_addr_t *ram_addr);
ram_addr_t qemu_ram_addr_from_host_nofail(void *ptr);
void qemu_ram_set_idstr(ram_addr_t addr, const char *name, DeviceState *dev);

void cpu_physical_memory_rw(hwaddr addr, uint8_t *buf,
                            int len, int is_write);
static inline void cpu_physical_memory_read(hwaddr addr,
                                            void *buf, int len)
{
    cpu_physical_memory_rw(addr, buf, len, 0);
}
static inline void cpu_physical_memory_write(hwaddr addr,
                                             const void *buf, int len)
{
    cpu_physical_memory_rw(addr, (void *)buf, len, 1);
}
void *cpu_physical_memory_map(hwaddr addr,
                              hwaddr *plen,
                              int is_write);
void cpu_physical_memory_unmap(void *buffer, hwaddr len,
                               int is_write, hwaddr access_len);
void *cpu_register_map_client(void *opaque, void (*callback)(void *opaque));

bool cpu_physical_memory_is_io(hwaddr phys_addr);

/* Coalesced MMIO regions are areas where write operations can be reordered.
 * This usually implies that write operations are side-effect free.  This allows
 * batching which can make a major impact on performance when using
 * virtualization.
 */
void qemu_flush_coalesced_mmio_buffer(void);

uint32_t ldub_phys(hwaddr addr);
uint32_t lduw_le_phys(hwaddr addr);
uint32_t lduw_be_phys(hwaddr addr);
uint32_t ldl_le_phys(hwaddr addr);
uint32_t ldl_be_phys(hwaddr addr);
uint64_t ldq_le_phys(hwaddr addr);
uint64_t ldq_be_phys(hwaddr addr);
void stb_phys(hwaddr addr, uint32_t val);
void stw_le_phys(hwaddr addr, uint32_t val);
void stw_be_phys(hwaddr addr, uint32_t val);
void stl_le_phys(hwaddr addr, uint32_t val);
void stl_be_phys(hwaddr addr, uint32_t val);
void stq_le_phys(hwaddr addr, uint64_t val);
void stq_be_phys(hwaddr addr, uint64_t val);

#ifdef NEED_CPU_H
uint32_t lduw_phys(hwaddr addr);
uint32_t ldl_phys(hwaddr addr);
uint64_t ldq_phys(hwaddr addr);
void stl_phys_notdirty(hwaddr addr, uint32_t val);
void stq_phys_notdirty(hwaddr addr, uint64_t val);
void stw_phys(hwaddr addr, uint32_t val);
void stl_phys(hwaddr addr, uint32_t val);
void stq_phys(hwaddr addr, uint64_t val);
#endif

void cpu_physical_memory_write_rom(hwaddr addr,
                                   const uint8_t *buf, int len);

extern struct MemoryRegion io_mem_ram;
extern struct MemoryRegion io_mem_rom;
extern struct MemoryRegion io_mem_unassigned;
extern struct MemoryRegion io_mem_notdirty;

#endif

#endif /* !CPU_COMMON_H */
