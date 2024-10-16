/*
 * DMA helper functions
 *
 * Copyright (c) 2009, 2020 Red Hat
 *
 * This work is licensed under the terms of the GNU General Public License
 * (GNU GPL), version 2 or later.
 */

#ifndef DMA_H
#define DMA_H

#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "block/block.h"
#include "block/accounting.h"

typedef enum {
    DMA_DIRECTION_TO_DEVICE = 0,
    DMA_DIRECTION_FROM_DEVICE = 1,
} DMADirection;

/*
 * When an IOMMU is present, bus addresses become distinct from
 * CPU/memory physical addresses and may be a different size.  Because
 * the IOVA size depends more on the bus than on the platform, we more
 * or less have to treat these as 64-bit always to cover all (or at
 * least most) cases.
 */
typedef uint64_t dma_addr_t;

#define DMA_ADDR_BITS 64
#define DMA_ADDR_FMT "%" PRIx64

typedef struct ScatterGatherEntry ScatterGatherEntry;

struct QEMUSGList {
    ScatterGatherEntry *sg;
    int nsg;
    int nalloc;
    dma_addr_t size;
    DeviceState *dev;
    AddressSpace *as;
};

static inline void dma_barrier(AddressSpace *as, DMADirection dir)
{
    /*
     * This is called before DMA read and write operations
     * unless the _relaxed form is used and is responsible
     * for providing some sane ordering of accesses vs
     * concurrently running VCPUs.
     *
     * Users of map(), unmap() or lower level st/ld_*
     * operations are responsible for providing their own
     * ordering via barriers.
     *
     * This primitive implementation does a simple smp_mb()
     * before each operation which provides pretty much full
     * ordering.
     *
     * A smarter implementation can be devised if needed to
     * use lighter barriers based on the direction of the
     * transfer, the DMA context, etc...
     */
    smp_mb();
}

/* Checks that the given range of addresses is valid for DMA.  This is
 * useful for certain cases, but usually you should just use
 * dma_memory_{read,write}() and check for errors */
static inline bool dma_memory_valid(AddressSpace *as,
                                    dma_addr_t addr, dma_addr_t len,
                                    DMADirection dir, MemTxAttrs attrs)
{
    return address_space_access_valid(as, addr, len,
                                      dir == DMA_DIRECTION_FROM_DEVICE,
                                      attrs);
}

static inline MemTxResult dma_memory_rw_relaxed(AddressSpace *as,
                                                dma_addr_t addr,
                                                void *buf, dma_addr_t len,
                                                DMADirection dir,
                                                MemTxAttrs attrs)
{
    return address_space_rw(as, addr, attrs,
                            buf, len, dir == DMA_DIRECTION_FROM_DEVICE);
}

static inline MemTxResult dma_memory_read_relaxed(AddressSpace *as,
                                                  dma_addr_t addr,
                                                  void *buf, dma_addr_t len)
{
    return dma_memory_rw_relaxed(as, addr, buf, len,
                                 DMA_DIRECTION_TO_DEVICE,
                                 MEMTXATTRS_UNSPECIFIED);
}

static inline MemTxResult dma_memory_write_relaxed(AddressSpace *as,
                                                   dma_addr_t addr,
                                                   const void *buf,
                                                   dma_addr_t len)
{
    return dma_memory_rw_relaxed(as, addr, (void *)buf, len,
                                 DMA_DIRECTION_FROM_DEVICE,
                                 MEMTXATTRS_UNSPECIFIED);
}

/**
 * dma_memory_rw: Read from or write to an address space from DMA controller.
 *
 * Return a MemTxResult indicating whether the operation succeeded
 * or failed (eg unassigned memory, device rejected the transaction,
 * IOMMU fault).
 *
 * @as: #AddressSpace to be accessed
 * @addr: address within that address space
 * @buf: buffer with the data transferred
 * @len: the number of bytes to read or write
 * @dir: indicates the transfer direction
 * @attrs: memory transaction attributes
 */
static inline MemTxResult dma_memory_rw(AddressSpace *as, dma_addr_t addr,
                                        void *buf, dma_addr_t len,
                                        DMADirection dir, MemTxAttrs attrs)
{
    dma_barrier(as, dir);

    return dma_memory_rw_relaxed(as, addr, buf, len, dir, attrs);
}

/**
 * dma_memory_read: Read from an address space from DMA controller.
 *
 * Return a MemTxResult indicating whether the operation succeeded
 * or failed (eg unassigned memory, device rejected the transaction,
 * IOMMU fault).  Called within RCU critical section.
 *
 * @as: #AddressSpace to be accessed
 * @addr: address within that address space
 * @buf: buffer with the data transferred
 * @len: length of the data transferred
 * @attrs: memory transaction attributes
 */
static inline MemTxResult dma_memory_read(AddressSpace *as, dma_addr_t addr,
                                          void *buf, dma_addr_t len,
                                          MemTxAttrs attrs)
{
    return dma_memory_rw(as, addr, buf, len,
                         DMA_DIRECTION_TO_DEVICE, attrs);
}

/**
 * dma_memory_write: Write to address space from DMA controller.
 *
 * Return a MemTxResult indicating whether the operation succeeded
 * or failed (eg unassigned memory, device rejected the transaction,
 * IOMMU fault).
 *
 * @as: #AddressSpace to be accessed
 * @addr: address within that address space
 * @buf: buffer with the data transferred
 * @len: the number of bytes to write
 * @attrs: memory transaction attributes
 */
static inline MemTxResult dma_memory_write(AddressSpace *as, dma_addr_t addr,
                                           const void *buf, dma_addr_t len,
                                           MemTxAttrs attrs)
{
    return dma_memory_rw(as, addr, (void *)buf, len,
                         DMA_DIRECTION_FROM_DEVICE, attrs);
}

/**
 * dma_memory_set: Fill memory with a constant byte from DMA controller.
 *
 * Return a MemTxResult indicating whether the operation succeeded
 * or failed (eg unassigned memory, device rejected the transaction,
 * IOMMU fault).
 *
 * @as: #AddressSpace to be accessed
 * @addr: address within that address space
 * @c: constant byte to fill the memory
 * @len: the number of bytes to fill with the constant byte
 * @attrs: memory transaction attributes
 */
MemTxResult dma_memory_set(AddressSpace *as, dma_addr_t addr,
                           uint8_t c, dma_addr_t len, MemTxAttrs attrs);

/**
 * dma_memory_map: Map a physical memory region into a host virtual address.
 *
 * May map a subset of the requested range, given by and returned in @plen.
 * May return %NULL and set *@plen to zero(0), if resources needed to perform
 * the mapping are exhausted.
 * Use only for reads OR writes - not for read-modify-write operations.
 *
 * @as: #AddressSpace to be accessed
 * @addr: address within that address space
 * @len: pointer to length of buffer; updated on return
 * @dir: indicates the transfer direction
 * @attrs: memory attributes
 */
static inline void *dma_memory_map(AddressSpace *as,
                                   dma_addr_t addr, dma_addr_t *len,
                                   DMADirection dir, MemTxAttrs attrs)
{
    hwaddr xlen = *len;
    void *p;

    p = address_space_map(as, addr, &xlen, dir == DMA_DIRECTION_FROM_DEVICE,
                          attrs);
    *len = xlen;
    return p;
}

/**
 * dma_memory_unmap: Unmaps a memory region previously mapped by dma_memory_map()
 *
 * Will also mark the memory as dirty if @dir == %DMA_DIRECTION_FROM_DEVICE.
 * @access_len gives the amount of memory that was actually read or written
 * by the caller.
 *
 * @as: #AddressSpace used
 * @buffer: host pointer as returned by dma_memory_map()
 * @len: buffer length as returned by dma_memory_map()
 * @dir: indicates the transfer direction
 * @access_len: amount of data actually transferred
 */
static inline void dma_memory_unmap(AddressSpace *as,
                                    void *buffer, dma_addr_t len,
                                    DMADirection dir, dma_addr_t access_len)
{
    address_space_unmap(as, buffer, (hwaddr)len,
                        dir == DMA_DIRECTION_FROM_DEVICE, access_len);
}

#define DEFINE_LDST_DMA(_lname, _sname, _bits, _end) \
    static inline MemTxResult ld##_lname##_##_end##_dma(AddressSpace *as, \
                                                        dma_addr_t addr, \
                                                        uint##_bits##_t *pval, \
                                                        MemTxAttrs attrs) \
    { \
        MemTxResult res = dma_memory_read(as, addr, pval, (_bits) / 8, attrs); \
        _end##_bits##_to_cpus(pval); \
        return res; \
    } \
    static inline MemTxResult st##_sname##_##_end##_dma(AddressSpace *as, \
                                                        dma_addr_t addr, \
                                                        uint##_bits##_t val, \
                                                        MemTxAttrs attrs) \
    { \
        val = cpu_to_##_end##_bits(val); \
        return dma_memory_write(as, addr, &val, (_bits) / 8, attrs); \
    }

static inline MemTxResult ldub_dma(AddressSpace *as, dma_addr_t addr,
                                   uint8_t *val, MemTxAttrs attrs)
{
    return dma_memory_read(as, addr, val, 1, attrs);
}

static inline MemTxResult stb_dma(AddressSpace *as, dma_addr_t addr,
                                  uint8_t val, MemTxAttrs attrs)
{
    return dma_memory_write(as, addr, &val, 1, attrs);
}

DEFINE_LDST_DMA(uw, w, 16, le);
DEFINE_LDST_DMA(l, l, 32, le);
DEFINE_LDST_DMA(q, q, 64, le);
DEFINE_LDST_DMA(uw, w, 16, be);
DEFINE_LDST_DMA(l, l, 32, be);
DEFINE_LDST_DMA(q, q, 64, be);

#undef DEFINE_LDST_DMA

struct ScatterGatherEntry {
    dma_addr_t base;
    dma_addr_t len;
};

void qemu_sglist_init(QEMUSGList *qsg, DeviceState *dev, int alloc_hint,
                      AddressSpace *as);
void qemu_sglist_add(QEMUSGList *qsg, dma_addr_t base, dma_addr_t len);
void qemu_sglist_destroy(QEMUSGList *qsg);

typedef BlockAIOCB *DMAIOFunc(int64_t offset, QEMUIOVector *iov,
                              BlockCompletionFunc *cb, void *cb_opaque,
                              void *opaque);

BlockAIOCB *dma_blk_io(AioContext *ctx,
                       QEMUSGList *sg, uint64_t offset, uint32_t align,
                       DMAIOFunc *io_func, void *io_func_opaque,
                       BlockCompletionFunc *cb, void *opaque, DMADirection dir);
BlockAIOCB *dma_blk_read(BlockBackend *blk,
                         QEMUSGList *sg, uint64_t offset, uint32_t align,
                         BlockCompletionFunc *cb, void *opaque);
BlockAIOCB *dma_blk_write(BlockBackend *blk,
                          QEMUSGList *sg, uint64_t offset, uint32_t align,
                          BlockCompletionFunc *cb, void *opaque);
MemTxResult dma_buf_read(void *ptr, dma_addr_t len, dma_addr_t *residual,
                         QEMUSGList *sg, MemTxAttrs attrs);
MemTxResult dma_buf_write(void *ptr, dma_addr_t len, dma_addr_t *residual,
                          QEMUSGList *sg, MemTxAttrs attrs);

void dma_acct_start(BlockBackend *blk, BlockAcctCookie *cookie,
                    QEMUSGList *sg, enum BlockAcctType type);

/**
 * dma_aligned_pow2_mask: Return the address bit mask of the largest
 * power of 2 size less or equal than @end - @start + 1, aligned with @start,
 * and bounded by 1 << @max_addr_bits bits.
 *
 * @start: range start address
 * @end: range end address (greater than @start)
 * @max_addr_bits: max address bits (<= 64)
 */
uint64_t dma_aligned_pow2_mask(uint64_t start, uint64_t end,
                               int max_addr_bits);

#endif
