/*
 * A sparse memory device. Useful for fuzzing
 *
 * Copyright Red Hat Inc., 2021
 *
 * Authors:
 *  Alexander Bulekov   <alxndr@bu.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "qapi/error.h"
#include "qemu/units.h"
#include "sysemu/qtest.h"
#include "hw/mem/sparse-mem.h"

#define SPARSE_MEM(obj) OBJECT_CHECK(SparseMemState, (obj), TYPE_SPARSE_MEM)
#define SPARSE_BLOCK_SIZE 0x1000

typedef struct SparseMemState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
    uint64_t baseaddr;
    uint64_t length;
    uint64_t size_used;
    uint64_t maxsize;
    GHashTable *mapped;
} SparseMemState;

typedef struct sparse_mem_block {
    uint8_t data[SPARSE_BLOCK_SIZE];
} sparse_mem_block;

static uint64_t sparse_mem_read(void *opaque, hwaddr addr, unsigned int size)
{
    SparseMemState *s = opaque;
    uint64_t ret = 0;
    size_t pfn = addr / SPARSE_BLOCK_SIZE;
    size_t offset = addr % SPARSE_BLOCK_SIZE;
    sparse_mem_block *block;

    block = g_hash_table_lookup(s->mapped, (void *)pfn);
    if (block) {
        assert(offset + size <= sizeof(block->data));
        memcpy(&ret, block->data + offset, size);
    }
    return ret;
}

static void sparse_mem_write(void *opaque, hwaddr addr, uint64_t v,
                             unsigned int size)
{
    SparseMemState *s = opaque;
    size_t pfn = addr / SPARSE_BLOCK_SIZE;
    size_t offset = addr % SPARSE_BLOCK_SIZE;
    sparse_mem_block *block;

    if (!g_hash_table_lookup(s->mapped, (void *)pfn) &&
        s->size_used + SPARSE_BLOCK_SIZE < s->maxsize && v) {
        g_hash_table_insert(s->mapped, (void *)pfn,
                            g_new0(sparse_mem_block, 1));
        s->size_used += sizeof(block->data);
    }
    block = g_hash_table_lookup(s->mapped, (void *)pfn);
    if (!block) {
        return;
    }

    assert(offset + size <= sizeof(block->data));

    memcpy(block->data + offset, &v, size);

}

static const MemoryRegionOps sparse_mem_ops = {
    .read = sparse_mem_read,
    .write = sparse_mem_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
            .min_access_size = 1,
            .max_access_size = 8,
            .unaligned = false,
        },
};

static Property sparse_mem_properties[] = {
    /* The base address of the memory */
    DEFINE_PROP_UINT64("baseaddr", SparseMemState, baseaddr, 0x0),
    /* The length of the sparse memory region */
    DEFINE_PROP_UINT64("length", SparseMemState, length, UINT64_MAX),
    /* Max amount of actual memory that can be used to back the sparse memory */
    DEFINE_PROP_UINT64("maxsize", SparseMemState, maxsize, 10 * MiB),
    DEFINE_PROP_END_OF_LIST(),
};

MemoryRegion *sparse_mem_init(uint64_t addr, uint64_t length)
{
    DeviceState *dev;

    dev = qdev_new(TYPE_SPARSE_MEM);
    qdev_prop_set_uint64(dev, "baseaddr", addr);
    qdev_prop_set_uint64(dev, "length", length);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map_overlap(SYS_BUS_DEVICE(dev), 0, addr, -10000);
    return &SPARSE_MEM(dev)->mmio;
}

static void sparse_mem_realize(DeviceState *dev, Error **errp)
{
    SparseMemState *s = SPARSE_MEM(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    if (!qtest_enabled()) {
        error_setg(errp, "sparse_mem device should only be used "
                         "for testing with QTest");
        return;
    }

    assert(s->baseaddr + s->length > s->baseaddr);

    s->mapped = g_hash_table_new(NULL, NULL);
    memory_region_init_io(&s->mmio, OBJECT(s), &sparse_mem_ops, s,
                          "sparse-mem", s->length);
    sysbus_init_mmio(sbd, &s->mmio);
}

static void sparse_mem_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, sparse_mem_properties);

    dc->desc = "Sparse Memory Device";
    dc->realize = sparse_mem_realize;
}

static const TypeInfo sparse_mem_types[] = {
    {
        .name = TYPE_SPARSE_MEM,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(SparseMemState),
        .class_init = sparse_mem_class_init,
    },
};
DEFINE_TYPES(sparse_mem_types);
