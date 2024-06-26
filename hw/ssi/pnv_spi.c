/*
 * QEMU PowerPC SPI model
 *
 * Copyright (c) 2024, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/qdev-properties.h"
#include "hw/ppc/pnv_xscom.h"
#include "hw/ssi/pnv_spi.h"
#include "hw/ssi/pnv_spi_regs.h"
#include "hw/ssi/ssi.h"
#include <libfdt.h>
#include "hw/irq.h"
#include "trace.h"

/*
 * Macro from include/hw/ppc/fdt.h
 * fdt.h cannot be included here as it contain ppc target specific dependency.
 */
#define _FDT(exp)                                                  \
    do {                                                           \
        int _ret = (exp);                                          \
        if (_ret < 0) {                                            \
            qemu_log_mask(LOG_GUEST_ERROR,                         \
                    "error creating device tree: %s: %s",          \
                    #exp, fdt_strerror(_ret));                     \
            exit(1);                                               \
        }                                                          \
    } while (0)

static uint64_t pnv_spi_xscom_read(void *opaque, hwaddr addr, unsigned size)
{
    PnvSpi *s = PNV_SPI(opaque);
    uint32_t reg = addr >> 3;
    uint64_t val = ~0ull;

    switch (reg) {
    case ERROR_REG:
    case SPI_CTR_CFG_REG:
    case CONFIG_REG1:
    case SPI_CLK_CFG_REG:
    case SPI_MM_REG:
    case SPI_XMIT_DATA_REG:
        val = s->regs[reg];
        break;
    case SPI_RCV_DATA_REG:
        val = s->regs[reg];
        trace_pnv_spi_read_RDR(val);
        s->status = SETFIELD(SPI_STS_RDR_FULL, s->status, 0);
        break;
    case SPI_SEQ_OP_REG:
        val = 0;
        for (int i = 0; i < PNV_SPI_REG_SIZE; i++) {
            val = (val << 8) | s->seq_op[i];
        }
        break;
    case SPI_STS_REG:
        val = s->status;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "pnv_spi_regs: Invalid xscom "
                 "read at 0x%" PRIx32 "\n", reg);
    }

    trace_pnv_spi_read(addr, val);
    return val;
}

static void pnv_spi_xscom_write(void *opaque, hwaddr addr,
                                 uint64_t val, unsigned size)
{
    PnvSpi *s = PNV_SPI(opaque);
    uint32_t reg = addr >> 3;

    trace_pnv_spi_write(addr, val);

    switch (reg) {
    case ERROR_REG:
    case SPI_CTR_CFG_REG:
    case CONFIG_REG1:
    case SPI_MM_REG:
    case SPI_RCV_DATA_REG:
        s->regs[reg] = val;
        break;
    case SPI_CLK_CFG_REG:
        /*
         * To reset the SPI controller write the sequence 0x5 0xA to
         * reset_control field
         */
        if ((GETFIELD(SPI_CLK_CFG_RST_CTRL, s->regs[SPI_CLK_CFG_REG]) == 0x5)
             && (GETFIELD(SPI_CLK_CFG_RST_CTRL, val) == 0xA)) {
                /* SPI controller reset sequence completed, resetting */
            s->regs[reg] = SPI_CLK_CFG_HARD_RST;
        } else {
            s->regs[reg] = val;
        }
        break;
    case SPI_XMIT_DATA_REG:
        /*
         * Writing to the transmit data register causes the transmit data
         * register full status bit in the status register to be set.  Writing
         * when the transmit data register full status bit is already set
         * causes a "Resource Not Available" condition.  This is not possible
         * in the model since writes to this register are not asynchronous to
         * the operation sequence like it would be in hardware.
         */
        s->regs[reg] = val;
        trace_pnv_spi_write_TDR(val);
        s->status = SETFIELD(SPI_STS_TDR_FULL, s->status, 1);
        s->status = SETFIELD(SPI_STS_TDR_UNDERRUN, s->status, 0);
        break;
    case SPI_SEQ_OP_REG:
        for (int i = 0; i < PNV_SPI_REG_SIZE; i++) {
            s->seq_op[i] = (val >> (56 - i * 8)) & 0xFF;
        }
        break;
    case SPI_STS_REG:
        /* other fields are ignore_write */
        s->status = SETFIELD(SPI_STS_RDR_OVERRUN, s->status,
                                  GETFIELD(SPI_STS_RDR, val));
        s->status = SETFIELD(SPI_STS_TDR_OVERRUN, s->status,
                                  GETFIELD(SPI_STS_TDR, val));
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "pnv_spi_regs: Invalid xscom "
                 "write at 0x%" PRIx32 "\n", reg);
    }
    return;
}

static const MemoryRegionOps pnv_spi_xscom_ops = {
    .read = pnv_spi_xscom_read,
    .write = pnv_spi_xscom_write,
    .valid.min_access_size = 8,
    .valid.max_access_size = 8,
    .impl.min_access_size = 8,
    .impl.max_access_size = 8,
    .endianness = DEVICE_BIG_ENDIAN,
};

static Property pnv_spi_properties[] = {
    DEFINE_PROP_UINT32("spic_num", PnvSpi, spic_num, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void pnv_spi_realize(DeviceState *dev, Error **errp)
{
    PnvSpi *s = PNV_SPI(dev);
    g_autofree char *name = g_strdup_printf(TYPE_PNV_SPI_BUS ".%d",
                    s->spic_num);
    s->ssi_bus = ssi_create_bus(dev, name);
    s->cs_line = g_new0(qemu_irq, 1);
    qdev_init_gpio_out_named(DEVICE(s), s->cs_line, "cs", 1);

    /* spi scoms */
    pnv_xscom_region_init(&s->xscom_spic_regs, OBJECT(s), &pnv_spi_xscom_ops,
                          s, "xscom-spi", PNV10_XSCOM_PIB_SPIC_SIZE);
}

static int pnv_spi_dt_xscom(PnvXScomInterface *dev, void *fdt,
                             int offset)
{
    PnvSpi *s = PNV_SPI(dev);
    g_autofree char *name;
    int s_offset;
    const char compat[] = "ibm,power10-spi";
    uint32_t spic_pcba = PNV10_XSCOM_PIB_SPIC_BASE +
        s->spic_num * PNV10_XSCOM_PIB_SPIC_SIZE;
    uint32_t reg[] = {
        cpu_to_be32(spic_pcba),
        cpu_to_be32(PNV10_XSCOM_PIB_SPIC_SIZE)
    };
    name = g_strdup_printf("pnv_spi@%x", spic_pcba);
    s_offset = fdt_add_subnode(fdt, offset, name);
    _FDT(s_offset);

    _FDT(fdt_setprop(fdt, s_offset, "reg", reg, sizeof(reg)));
    _FDT(fdt_setprop(fdt, s_offset, "compatible", compat, sizeof(compat)));
    _FDT((fdt_setprop_cell(fdt, s_offset, "spic_num#", s->spic_num)));
    return 0;
}

static void pnv_spi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PnvXScomInterfaceClass *xscomc = PNV_XSCOM_INTERFACE_CLASS(klass);

    xscomc->dt_xscom = pnv_spi_dt_xscom;

    dc->desc = "PowerNV SPI";
    dc->realize = pnv_spi_realize;
    device_class_set_props(dc, pnv_spi_properties);
}

static const TypeInfo pnv_spi_info = {
    .name          = TYPE_PNV_SPI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PnvSpi),
    .class_init    = pnv_spi_class_init,
    .interfaces    = (InterfaceInfo[]) {
        { TYPE_PNV_XSCOM_INTERFACE },
        { }
    }
};

static void pnv_spi_register_types(void)
{
    type_register_static(&pnv_spi_info);
}

type_init(pnv_spi_register_types);
