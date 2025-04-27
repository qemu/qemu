/*
 * QEMU PowerPC PowerNV ADU unit
 *
 * The ADU unit actually implements XSCOM, which is the bridge between MMIO
 * and PIB. However it also includes control and status registers and other
 * functions that are exposed as PIB (xscom) registers.
 *
 * To keep things simple, pnv_xscom.c remains the XSCOM bridge
 * implementation, and pnv_adu.c implements the ADU registers and other
 * functions.
 *
 * Copyright (c) 2024, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"

#include "hw/qdev-properties.h"
#include "hw/ppc/pnv.h"
#include "hw/ppc/pnv_adu.h"
#include "hw/ppc/pnv_chip.h"
#include "hw/ppc/pnv_lpc.h"
#include "hw/ppc/pnv_xscom.h"
#include "trace.h"

#define ADU_LPC_BASE_REG     0x40
#define ADU_LPC_CMD_REG      0x41
#define ADU_LPC_DATA_REG     0x42
#define ADU_LPC_STATUS_REG   0x43

static uint64_t pnv_adu_xscom_read(void *opaque, hwaddr addr, unsigned width)
{
    PnvADU *adu = PNV_ADU(opaque);
    uint32_t offset = addr >> 3;
    uint64_t val = 0;

    switch (offset) {
    case 0x18:     /* Receive status reg */
    case 0x12:     /* log register */
    case 0x13:     /* error register */
        break;
    case ADU_LPC_BASE_REG:
        /*
         * LPC Address Map in Pervasive ADU Workbook
         *
         * return PNV10_LPCM_BASE(chip) & PPC_BITMASK(8, 31);
         * XXX: implement as class property, or get from LPC?
         */
        qemu_log_mask(LOG_UNIMP, "ADU: LPC_BASE_REG is not implemented\n");
        break;
    case ADU_LPC_CMD_REG:
        val = adu->lpc_cmd_reg;
        break;
    case ADU_LPC_DATA_REG:
        val = adu->lpc_data_reg;
        break;
    case ADU_LPC_STATUS_REG:
        val = PPC_BIT(0); /* ack / done */
        break;

    default:
        qemu_log_mask(LOG_UNIMP, "ADU Unimplemented read register: Ox%08x\n",
                                                                     offset);
    }

    trace_pnv_adu_xscom_read(addr, val);

    return val;
}

static bool lpc_cmd_read(PnvADU *adu)
{
    return !!(adu->lpc_cmd_reg & PPC_BIT(0));
}

static bool lpc_cmd_write(PnvADU *adu)
{
    return !lpc_cmd_read(adu);
}

static uint32_t lpc_cmd_addr(PnvADU *adu)
{
    return (adu->lpc_cmd_reg & PPC_BITMASK(32, 63)) >> PPC_BIT_NR(63);
}

static uint32_t lpc_cmd_size(PnvADU *adu)
{
    return (adu->lpc_cmd_reg & PPC_BITMASK(5, 11)) >> PPC_BIT_NR(11);
}

static void pnv_adu_xscom_write(void *opaque, hwaddr addr, uint64_t val,
                                unsigned width)
{
    PnvADU *adu = PNV_ADU(opaque);
    uint32_t offset = addr >> 3;

    trace_pnv_adu_xscom_write(addr, val);

    switch (offset) {
    case 0x18:     /* Receive status reg */
    case 0x12:     /* log register */
    case 0x13:     /* error register */
        break;

    case ADU_LPC_BASE_REG:
        qemu_log_mask(LOG_UNIMP,
                      "ADU: Changing LPC_BASE_REG is not implemented\n");
        break;

    case ADU_LPC_CMD_REG:
        adu->lpc_cmd_reg = val;
        if (lpc_cmd_read(adu)) {
            uint32_t lpc_addr = lpc_cmd_addr(adu);
            uint32_t lpc_size = lpc_cmd_size(adu);
            uint64_t data = 0;

            if (!is_power_of_2(lpc_size) || lpc_size > sizeof(data)) {
                qemu_log_mask(LOG_GUEST_ERROR, "ADU: Unsupported LPC access "
                                               "size:%" PRId32 "\n", lpc_size);
                break;
            }

            pnv_lpc_opb_read(adu->lpc, lpc_addr, (void *)&data, lpc_size);

            /*
             * ADU access is performed within 8-byte aligned sectors. Smaller
             * access sizes don't get formatted to the least significant byte,
             * but rather appear in the data reg at the same offset as the
             * address in memory. This shifts them into that position.
             */
            adu->lpc_data_reg = be64_to_cpu(data) >> ((lpc_addr & 7) * 8);
        }
        break;

    case ADU_LPC_DATA_REG:
        adu->lpc_data_reg = val;
        if (lpc_cmd_write(adu)) {
            uint32_t lpc_addr = lpc_cmd_addr(adu);
            uint32_t lpc_size = lpc_cmd_size(adu);
            uint64_t data;

            if (!is_power_of_2(lpc_size) || lpc_size > sizeof(data)) {
                qemu_log_mask(LOG_GUEST_ERROR, "ADU: Unsupported LPC access "
                                               "size:%" PRId32 "\n", lpc_size);
                break;
            }

            data = cpu_to_be64(val) >> ((lpc_addr & 7) * 8); /* See above */
            pnv_lpc_opb_write(adu->lpc, lpc_addr, (void *)&data, lpc_size);
        }
        break;

    case ADU_LPC_STATUS_REG:
        qemu_log_mask(LOG_UNIMP,
                      "ADU: Changing LPC_STATUS_REG is not implemented\n");
        break;

    default:
        qemu_log_mask(LOG_UNIMP, "ADU Unimplemented write register: Ox%08x\n",
                                                                     offset);
    }
}

const MemoryRegionOps pnv_adu_xscom_ops = {
    .read = pnv_adu_xscom_read,
    .write = pnv_adu_xscom_write,
    .valid.min_access_size = 8,
    .valid.max_access_size = 8,
    .impl.min_access_size = 8,
    .impl.max_access_size = 8,
    .endianness = DEVICE_BIG_ENDIAN,
};

static void pnv_adu_realize(DeviceState *dev, Error **errp)
{
    PnvADU *adu = PNV_ADU(dev);

    assert(adu->lpc);

    /* XScom regions for ADU registers */
    pnv_xscom_region_init(&adu->xscom_regs, OBJECT(dev),
                          &pnv_adu_xscom_ops, adu, "xscom-adu",
                          PNV9_XSCOM_ADU_SIZE);
}

static const Property pnv_adu_properties[] = {
    DEFINE_PROP_LINK("lpc", PnvADU, lpc, TYPE_PNV_LPC, PnvLpcController *),
};

static void pnv_adu_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = pnv_adu_realize;
    dc->desc = "PowerNV ADU";
    device_class_set_props(dc, pnv_adu_properties);
    dc->user_creatable = false;
}

static const TypeInfo pnv_adu_type_info = {
    .name          = TYPE_PNV_ADU,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(PnvADU),
    .class_init    = pnv_adu_class_init,
    .interfaces    = (const InterfaceInfo[]) {
        { TYPE_PNV_XSCOM_INTERFACE },
        { } },
};

static void pnv_adu_register_types(void)
{
    type_register_static(&pnv_adu_type_info);
}

type_init(pnv_adu_register_types);
