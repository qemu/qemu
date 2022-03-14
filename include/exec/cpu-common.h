#ifndef CPU_COMMON_H
#define CPU_COMMON_H

/* CPU interfaces that are target independent.  */

#ifndef CONFIG_USER_ONLY
#include "exec/hwaddr.h"
#endif

/**
 * vaddr:
 * Type wide enough to contain any #target_ulong virtual address.
 */
typedef uint64_t vaddr;
#define VADDR_PRId PRId64
#define VADDR_PRIu PRIu64
#define VADDR_PRIo PRIo64
#define VADDR_PRIx PRIx64
#define VADDR_PRIX PRIX64
#define VADDR_MAX UINT64_MAX

/* Using intptr_t ensures that qemu_*_page_mask is sign-extended even
 * when intptr_t is 32-bit and we are aligning a long long.
 */
extern uintptr_t qemu_host_page_size;
extern intptr_t qemu_host_page_mask;

#define HOST_PAGE_ALIGN(addr) ROUND_UP((addr), qemu_host_page_size)
#define REAL_HOST_PAGE_ALIGN(addr) ROUND_UP((addr), qemu_real_host_page_size)

/* The CPU list lock nests outside page_(un)lock or mmap_(un)lock */
void qemu_init_cpu_list(void);
void cpu_list_lock(void);
void cpu_list_unlock(void);

void tcg_flush_softmmu_tlb(CPUState *cs);

void tcg_iommu_init_notifier_list(CPUState *cpu);
void tcg_iommu_free_notifier_list(CPUState *cpu);

#if !defined(CONFIG_USER_ONLY)

enum device_endian {
    DEVICE_NATIVE_ENDIAN,
    DEVICE_BIG_ENDIAN,
    DEVICE_LITTLE_ENDIAN,
};

#if defined(HOST_WORDS_BIGENDIAN)
#define DEVICE_HOST_ENDIAN DEVICE_BIG_ENDIAN
#else
#define DEVICE_HOST_ENDIAN DEVICE_LITTLE_ENDIAN
#endif

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

void qemu_ram_remap(ram_addr_t addr, ram_addr_t length);
/* This should not be used by devices.  */
ram_addr_t qemu_ram_addr_from_host(void *ptr);
RAMBlock *qemu_ram_block_by_name(const char *name);
RAMBlock *qemu_ram_block_from_host(void *ptr, bool round_offset,
                                   ram_addr_t *offset);
ram_addr_t qemu_ram_block_host_offset(RAMBlock *rb, void *host);
void qemu_ram_set_idstr(RAMBlock *block, const char *name, DeviceState *dev);
void qemu_ram_unset_idstr(RAMBlock *block);
const char *qemu_ram_get_idstr(RAMBlock *rb);
void *qemu_ram_get_host_addr(RAMBlock *rb);
ram_addr_t qemu_ram_get_offset(RAMBlock *rb);
ram_addr_t qemu_ram_get_used_length(RAMBlock *rb);
ram_addr_t qemu_ram_get_max_length(RAMBlock *rb);
bool qemu_ram_is_shared(RAMBlock *rb);
bool qemu_ram_is_noreserve(RAMBlock *rb);
bool qemu_ram_is_uf_zeroable(RAMBlock *rb);
void qemu_ram_set_uf_zeroable(RAMBlock *rb);
bool qemu_ram_is_migratable(RAMBlock *rb);
void qemu_ram_set_migratable(RAMBlock *rb);
void qemu_ram_unset_migratable(RAMBlock *rb);

size_t qemu_ram_pagesize(RAMBlock *block);
size_t qemu_ram_pagesize_largest(void);

/**
 * cpu_address_space_init:
 * @cpu: CPU to add this address space to
 * @asidx: integer index of this address space
 * @prefix: prefix to be used as name of address space
 * @mr: the root memory region of address space
 *
 * Add the specified address space to the CPU's cpu_ases list.
 * The address space added with @asidx 0 is the one used for the
 * convenience pointer cpu->as.
 * The target-specific code which registers ASes is responsible
 * for defining what semantics address space 0, 1, 2, etc have.
 *
 * Before the first call to this function, the caller must set
 * cpu->num_ases to the total number of address spaces it needs
 * to support.
 *
 * Note that with KVM only one address space is supported.
 */
void cpu_address_space_init(CPUState *cpu, int asidx,
                            const char *prefix, MemoryRegion *mr);

void cpu_physical_memory_rw(hwaddr addr, void *buf,
                            hwaddr len, bool is_write);
static inline void cpu_physical_memory_read(hwaddr addr,
                                            void *buf, hwaddr len)
{
    cpu_physical_memory_rw(addr, buf, len, false);
}
static inline void cpu_physical_memory_write(hwaddr addr,
                                             const void *buf, hwaddr len)
{
    cpu_physical_memory_rw(addr, (void *)buf, len, true);
}
void cpu_reloading_memory_map(void);
void *cpu_physical_memory_map(hwaddr addr,
                              hwaddr *plen,
                              bool is_write);
void cpu_physical_memory_unmap(void *buffer, hwaddr len,
                               bool is_write, hwaddr access_len);
void cpu_register_map_client(QEMUBH *bh);
void cpu_unregister_map_client(QEMUBH *bh);

bool cpu_physical_memory_is_io(hwaddr phys_addr);

/* Coalesced MMIO regions are areas where write operations can be reordered.
 * This usually implies that write operations are side-effect free.  This allows
 * batching which can make a major impact on performance when using
 * virtualization.
 */
void qemu_flush_coalesced_mmio_buffer(void);

void cpu_flush_icache_range(hwaddr start, hwaddr len);

typedef int (RAMBlockIterFunc)(RAMBlock *rb, void *opaque);

int qemu_ram_foreach_block(RAMBlockIterFunc func, void *opaque);
int ram_block_discard_range(RAMBlock *rb, uint64_t start, size_t length);

#endif

/* Returns: 0 on success, -1 on error */
int cpu_memory_rw_debug(CPUState *cpu, vaddr addr,
                        void *ptr, size_t len, bool is_write);

/* vl.c */
extern int singlestep;

void list_cpus(const char *optarg);

#endif /* CPU_COMMON_H */
