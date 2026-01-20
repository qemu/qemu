/*
 * A test device for IOMMU
 *
 * Copyright (c) 2026 Phytium Technology
 *
 * Author:
 *  Tao Tang <tangtao1634@phytium.com.cn>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "system/address-spaces.h"
#include "system/memory.h"
#include "trace.h"
#include "hw/pci/pci_device.h"
#include "hw/core/qdev-properties.h"
#include "qom/object.h"
#include "hw/misc/iommu-testdev.h"

#define TYPE_IOMMU_TESTDEV "iommu-testdev"
OBJECT_DECLARE_SIMPLE_TYPE(IOMMUTestDevState, IOMMU_TESTDEV)

struct IOMMUTestDevState {
    PCIDevice parent_obj;
    MemoryRegion bar0;
    uint64_t dma_vaddr;
    uint64_t dma_paddr;
    uint32_t dma_len;
    uint32_t dma_result;
    bool dma_armed; /* armed until a trigger consumes the request */

    AddressSpace *dma_as;   /* IOMMU-mediated DMA AS for this device */
    uint32_t dma_attrs_cfg; /* bit0 secure, bits[2:1] space, bit3 valid */
};

static bool iommu_testdev_attrs_inconsistent(uint32_t cfg)
{
    uint32_t space;
    bool secure;

    if (!ITD_ATTRS_GET_SPACE_VALID(cfg)) {
        return false;
    }

    space = ITD_ATTRS_GET_SPACE(cfg);
    secure = ITD_ATTRS_GET_SECURE(cfg);

    if (space == ITD_ATTRS_SPACE_SECURE || space == ITD_ATTRS_SPACE_NONSECURE) {
        return secure != (space == ITD_ATTRS_SPACE_SECURE);
    }

    return false;
}

static void iommu_testdev_maybe_run_dma(IOMMUTestDevState *s)
{
    uint32_t expected_val, actual_val;
    g_autofree uint8_t *write_buf = NULL;
    g_autofree uint8_t *read_buf = NULL;
    MemTxResult write_res, read_res;
    MemTxAttrs attrs = {};
    AddressSpace *as;
    bool space_valid;

    if (!s->dma_armed) {
        s->dma_result = ITD_DMA_ERR_NOT_ARMED;
        trace_iommu_testdev_dma_result(s->dma_result);
        return;
    }
    trace_iommu_testdev_dma_start();

    if (!s->dma_len) {
        s->dma_result = ITD_DMA_ERR_BAD_LEN;
        goto out;
    }

    write_buf = g_malloc(s->dma_len);
    read_buf = g_malloc(s->dma_len);

    /* Initialize MemTxAttrs from generic register. */
    attrs.secure = ITD_ATTRS_GET_SECURE(s->dma_attrs_cfg);

    space_valid = ITD_ATTRS_GET_SPACE_VALID(s->dma_attrs_cfg);
    if (space_valid) {
        /* The 'space' field in MemTxAttrs is ARM-specific. */
        attrs.space = ITD_ATTRS_GET_SPACE(s->dma_attrs_cfg);
    } else {
        /* Default to Non-Secure when space is not valid. */
        attrs.space = ITD_ATTRS_SPACE_NONSECURE;
    }

    if (iommu_testdev_attrs_inconsistent(s->dma_attrs_cfg)) {
        s->dma_result = ITD_DMA_ERR_BAD_ATTRS;
        goto out;
    }

    as = s->dma_as;

    /* Step 1: Write ITD_DMA_WRITE_VAL to DMA address */
    trace_iommu_testdev_dma_write(s->dma_vaddr, s->dma_len);

    for (int i = 0; i < s->dma_len; i++) {
        /* Data is written in little-endian order */
        write_buf[i] = (ITD_DMA_WRITE_VAL >> ((i % 4) * 8)) & 0xff;
    }
    write_res = dma_memory_write(as, s->dma_vaddr, write_buf,
                                 s->dma_len, attrs);

    if (write_res != MEMTX_OK) {
        s->dma_result = ITD_DMA_ERR_TX_FAIL;
        goto out;
    }

    /* Step 2: Read back from the same DMA address */
    trace_iommu_testdev_dma_read(s->dma_vaddr, s->dma_len);

    read_res = address_space_read(&address_space_memory, s->dma_paddr,
                                  attrs, read_buf, s->dma_len);

    if (read_res != MEMTX_OK) {
        s->dma_result = ITD_DMA_ERR_RD_FAIL;
        goto out;
    }

    /* Step 3: Verify the read data matches what we wrote */
    for (int i = 0; i < s->dma_len; i += 4) {
        int remaining_bytes = MIN(4, s->dma_len - i);

        expected_val = 0;
        actual_val = 0;

        for (int j = 0; j < remaining_bytes; j++) {
            expected_val |= ((uint32_t)write_buf[i + j]) << (j * 8);
            actual_val |= ((uint32_t)read_buf[i + j]) << (j * 8);
        }

        trace_iommu_testdev_dma_verify(expected_val, actual_val);

        if (expected_val != actual_val) {
            s->dma_result = ITD_DMA_ERR_MISMATCH;
            goto out;
        }
    }

    /* All checks passed */
    s->dma_result = 0;
out:
    trace_iommu_testdev_dma_result(s->dma_result);
    s->dma_armed = false;
}

static uint64_t iommu_testdev_mmio_read(void *opaque, hwaddr addr,
                                        unsigned size)
{
    IOMMUTestDevState *s = opaque;
    uint64_t value = 0;

    switch (addr) {
    case ITD_REG_DMA_TRIGGERING:
        /*
         * This lets tests poll ITD_REG_DMA_RESULT to observe BUSY before
         * consuming the DMA.
         */
        iommu_testdev_maybe_run_dma(s);
        value = 0;
        break;
    case ITD_REG_DMA_GVA_LO:
        value = (uint32_t)(s->dma_vaddr & 0xffffffffu);
        break;
    case ITD_REG_DMA_GVA_HI:
        value = (uint32_t)(s->dma_vaddr >> 32);
        break;
    case ITD_REG_DMA_GPA_LO:
        value = (uint32_t)(s->dma_paddr & 0xffffffffu);
        break;
    case ITD_REG_DMA_GPA_HI:
        value = (uint32_t)(s->dma_paddr >> 32);
        break;
    case ITD_REG_DMA_LEN:
        value = s->dma_len;
        break;
    case ITD_REG_DMA_RESULT:
        value = s->dma_result;
        break;
    case ITD_REG_DMA_ATTRS:
        value = s->dma_attrs_cfg;
        break;
    default:
        value = 0;
        break;
    }

    trace_iommu_testdev_mmio_read(addr, value, size);
    return value;
}

static void iommu_testdev_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                                     unsigned size)
{
    IOMMUTestDevState *s = opaque;
    uint32_t data = val;

    trace_iommu_testdev_mmio_write(addr, val, size);

    switch (addr) {
    case ITD_REG_DMA_GVA_LO:
        s->dma_vaddr = (s->dma_vaddr & ~0xffffffffull) | data;
        break;
    case ITD_REG_DMA_GVA_HI:
        s->dma_vaddr = (s->dma_vaddr & 0xffffffffull) |
                       ((uint64_t)data << 32);
        break;
    case ITD_REG_DMA_GPA_LO:
        s->dma_paddr = (s->dma_paddr & ~0xffffffffull) | data;
        break;
    case ITD_REG_DMA_GPA_HI:
        s->dma_paddr = (s->dma_paddr & 0xffffffffull) |
                       ((uint64_t)data << 32);
        break;
    case ITD_REG_DMA_LEN:
        s->dma_len = data;
        break;
    case ITD_REG_DMA_RESULT:
        s->dma_result = data;
        break;
    case ITD_REG_DMA_DBELL:
        if (data & ITD_DMA_DBELL_ARM) {
            /* Arm the DMA operation; repeated arm is idempotent. */
            s->dma_armed = true;
            s->dma_result = ITD_DMA_RESULT_BUSY;
            trace_iommu_testdev_dma_armed(true);
        } else {
            /* Disarm the DMA operation */
            s->dma_armed = false;
            s->dma_result = ITD_DMA_RESULT_IDLE;
            trace_iommu_testdev_dma_armed(false);
        }
        break;
    case ITD_REG_DMA_ATTRS:
        s->dma_attrs_cfg = data;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps iommu_testdev_mmio_ops = {
    .read = iommu_testdev_mmio_read,
    .write = iommu_testdev_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void iommu_testdev_realize(PCIDevice *pdev, Error **errp)
{
    IOMMUTestDevState *s = IOMMU_TESTDEV(pdev);

    s->dma_vaddr = 0;
    s->dma_paddr = 0;
    s->dma_len = 0;
    s->dma_result = ITD_DMA_RESULT_IDLE;
    s->dma_armed = false;
    s->dma_attrs_cfg = ITD_ATTRS_SET_SPACE(0, ITD_ATTRS_SPACE_NONSECURE);
    s->dma_as = pci_device_iommu_address_space(pdev);

    memory_region_init_io(&s->bar0, OBJECT(pdev), &iommu_testdev_mmio_ops, s,
                          TYPE_IOMMU_TESTDEV ".bar0", BAR0_SIZE);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar0);
}

static void iommu_testdev_reset(DeviceState *dev)
{
    IOMMUTestDevState *s = IOMMU_TESTDEV(dev);

    s->dma_vaddr = 0;
    s->dma_paddr = 0;
    s->dma_len = 0;
    s->dma_result = ITD_DMA_RESULT_IDLE;
    s->dma_armed = false;
    s->dma_attrs_cfg = ITD_ATTRS_SET_SPACE(0, ITD_ATTRS_SPACE_NONSECURE);
}

static void iommu_testdev_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(klass);

    pc->realize = iommu_testdev_realize;
    pc->vendor_id = IOMMU_TESTDEV_VENDOR_ID;
    pc->device_id = IOMMU_TESTDEV_DEVICE_ID;
    pc->revision = 0;
    pc->class_id = PCI_CLASS_OTHERS;
    dc->desc = "A test device for IOMMU";
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    device_class_set_legacy_reset(dc, iommu_testdev_reset);
}

static const TypeInfo iommu_testdev_info = {
    .name          = TYPE_IOMMU_TESTDEV,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(IOMMUTestDevState),
    .class_init    = iommu_testdev_class_init,
    .interfaces    = (const InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { }
    },
};

static void iommu_testdev_register_types(void)
{
    type_register_static(&iommu_testdev_info);
}

type_init(iommu_testdev_register_types);
