/*
 * ADC registers for Xilinx Zynq Platform
 *
 * Copyright (c) 2015 Guenter Roeck
 * Based on hw/misc/zynq_slcr.c, written by Michal Simek
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/misc/zynq-xadc.h"
#include "migration/vmstate.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qemu/module.h"

enum {
    CFG                = 0x000 / 4,
    INT_STS,
    INT_MASK,
    MSTS,
    CMDFIFO,
    RDFIFO,
    MCTL,
};

#define CFG_ENABLE              BIT(31)
#define CFG_CFIFOTH_SHIFT       20
#define CFG_CFIFOTH_LENGTH      4
#define CFG_DFIFOTH_SHIFT       16
#define CFG_DFIFOTH_LENGTH      4
#define CFG_WEDGE               BIT(13)
#define CFG_REDGE               BIT(12)
#define CFG_TCKRATE_SHIFT       8
#define CFG_TCKRATE_LENGTH      2

#define CFG_TCKRATE_DIV(x)      (0x1 << (x - 1))

#define CFG_IGAP_SHIFT          0
#define CFG_IGAP_LENGTH         5

#define INT_CFIFO_LTH           BIT(9)
#define INT_DFIFO_GTH           BIT(8)
#define INT_OT                  BIT(7)
#define INT_ALM_SHIFT           0
#define INT_ALM_LENGTH          7
#define INT_ALM_MASK            (((1 << INT_ALM_LENGTH) - 1) << INT_ALM_SHIFT)

#define INT_ALL (INT_CFIFO_LTH | INT_DFIFO_GTH | INT_OT | INT_ALM_MASK)

#define MSTS_CFIFO_LVL_SHIFT    16
#define MSTS_CFIFO_LVL_LENGTH   4
#define MSTS_DFIFO_LVL_SHIFT    12
#define MSTS_DFIFO_LVL_LENGTH   4
#define MSTS_CFIFOF             BIT(11)
#define MSTS_CFIFOE             BIT(10)
#define MSTS_DFIFOF             BIT(9)
#define MSTS_DFIFOE             BIT(8)
#define MSTS_OT                 BIT(7)
#define MSTS_ALM_SHIFT          0
#define MSTS_ALM_LENGTH         7

#define MCTL_RESET              BIT(4)

#define CMD_NOP                 0x00
#define CMD_READ                0x01
#define CMD_WRITE               0x02

static void zynq_xadc_update_ints(ZynqXADCState *s)
{

    /* We are fast, commands are actioned instantly so the CFIFO is always
     * empty (and below threshold).
     */
    s->regs[INT_STS] |= INT_CFIFO_LTH;

    if (s->xadc_dfifo_entries >
        extract32(s->regs[CFG], CFG_DFIFOTH_SHIFT, CFG_DFIFOTH_LENGTH)) {
        s->regs[INT_STS] |= INT_DFIFO_GTH;
    }

    qemu_set_irq(s->qemu_irq, !!(s->regs[INT_STS] & ~s->regs[INT_MASK]));
}

static void zynq_xadc_reset(DeviceState *d)
{
    ZynqXADCState *s = ZYNQ_XADC(d);

    s->regs[CFG] = 0x14 << CFG_IGAP_SHIFT |
                   CFG_TCKRATE_DIV(4) << CFG_TCKRATE_SHIFT | CFG_REDGE;
    s->regs[INT_STS] = INT_CFIFO_LTH;
    s->regs[INT_MASK] = 0xffffffff;
    s->regs[CMDFIFO] = 0;
    s->regs[RDFIFO] = 0;
    s->regs[MCTL] = MCTL_RESET;

    memset(s->xadc_regs, 0, sizeof(s->xadc_regs));
    memset(s->xadc_dfifo, 0, sizeof(s->xadc_dfifo));
    s->xadc_dfifo_entries = 0;

    zynq_xadc_update_ints(s);
}

static uint16_t xadc_pop_dfifo(ZynqXADCState *s)
{
    uint16_t rv = s->xadc_dfifo[0];
    int i;

    if (s->xadc_dfifo_entries > 0) {
        s->xadc_dfifo_entries--;
    }
    for (i = 0; i < s->xadc_dfifo_entries; i++) {
        s->xadc_dfifo[i] = s->xadc_dfifo[i + 1];
    }
    s->xadc_dfifo[s->xadc_dfifo_entries] = 0;
    zynq_xadc_update_ints(s);
    return rv;
}

static void xadc_push_dfifo(ZynqXADCState *s, uint16_t regval)
{
    if (s->xadc_dfifo_entries < ZYNQ_XADC_FIFO_DEPTH) {
        s->xadc_dfifo[s->xadc_dfifo_entries++] = s->xadc_read_reg_previous;
    }
    s->xadc_read_reg_previous = regval;
    zynq_xadc_update_ints(s);
}

static bool zynq_xadc_check_offset(hwaddr offset, bool rnw)
{
    switch (offset) {
    case CFG:
    case INT_MASK:
    case INT_STS:
    case MCTL:
        return true;
    case RDFIFO:
    case MSTS:
        return rnw;     /* read only */
    case CMDFIFO:
        return !rnw;    /* write only */
    default:
        return false;
    }
}

static uint64_t zynq_xadc_read(void *opaque, hwaddr offset, unsigned size)
{
    ZynqXADCState *s = opaque;
    int reg = offset / 4;
    uint32_t rv = 0;

    if (!zynq_xadc_check_offset(reg, true)) {
        qemu_log_mask(LOG_GUEST_ERROR, "zynq_xadc: Invalid read access to "
                      "addr %" HWADDR_PRIx "\n", offset);
        return 0;
    }

    switch (reg) {
    case CFG:
    case INT_MASK:
    case INT_STS:
    case MCTL:
        rv = s->regs[reg];
        break;
    case MSTS:
        rv = MSTS_CFIFOE;
        rv |= s->xadc_dfifo_entries << MSTS_DFIFO_LVL_SHIFT;
        if (!s->xadc_dfifo_entries) {
            rv |= MSTS_DFIFOE;
        } else if (s->xadc_dfifo_entries == ZYNQ_XADC_FIFO_DEPTH) {
            rv |= MSTS_DFIFOF;
        }
        break;
    case RDFIFO:
        rv = xadc_pop_dfifo(s);
        break;
    }
    return rv;
}

static void zynq_xadc_write(void *opaque, hwaddr offset, uint64_t val,
                            unsigned size)
{
    ZynqXADCState *s = (ZynqXADCState *)opaque;
    int reg = offset / 4;
    int xadc_reg;
    int xadc_cmd;
    int xadc_data;

    if (!zynq_xadc_check_offset(reg, false)) {
        qemu_log_mask(LOG_GUEST_ERROR, "zynq_xadc: Invalid write access "
                      "to addr %" HWADDR_PRIx "\n", offset);
        return;
    }

    switch (reg) {
    case CFG:
        s->regs[CFG] = val;
        break;
    case INT_STS:
        s->regs[INT_STS] &= ~val;
        break;
    case INT_MASK:
        s->regs[INT_MASK] = val & INT_ALL;
        break;
    case CMDFIFO:
        xadc_cmd  = extract32(val, 26,  4);
        xadc_reg  = extract32(val, 16, 10);
        xadc_data = extract32(val,  0, 16);

        if (s->regs[MCTL] & MCTL_RESET) {
            qemu_log_mask(LOG_GUEST_ERROR, "zynq_xadc: Sending command "
                          "while comm channel held in reset: %" PRIx32 "\n",
                          (uint32_t) val);
            break;
        }

        if (xadc_reg >= ZYNQ_XADC_NUM_ADC_REGS && xadc_cmd != CMD_NOP) {
            qemu_log_mask(LOG_GUEST_ERROR, "read/write op to invalid xadc "
                          "reg 0x%x\n", xadc_reg);
            break;
        }

        switch (xadc_cmd) {
        case CMD_READ:
            xadc_push_dfifo(s, s->xadc_regs[xadc_reg]);
            break;
        case CMD_WRITE:
            s->xadc_regs[xadc_reg] = xadc_data;
            /* fallthrough */
        case CMD_NOP:
            xadc_push_dfifo(s, 0);
            break;
        }
        break;
    case MCTL:
        s->regs[MCTL] = val & 0x00fffeff;
        break;
    }
    zynq_xadc_update_ints(s);
}

static const MemoryRegionOps xadc_ops = {
    .read = zynq_xadc_read,
    .write = zynq_xadc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void zynq_xadc_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    ZynqXADCState *s = ZYNQ_XADC(obj);

    memory_region_init_io(&s->iomem, obj, &xadc_ops, s, "zynq-xadc",
                          ZYNQ_XADC_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->qemu_irq);
}

static const VMStateDescription vmstate_zynq_xadc = {
    .name = "zynq-xadc",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, ZynqXADCState, ZYNQ_XADC_NUM_IO_REGS),
        VMSTATE_UINT16_ARRAY(xadc_regs, ZynqXADCState,
                             ZYNQ_XADC_NUM_ADC_REGS),
        VMSTATE_UINT16_ARRAY(xadc_dfifo, ZynqXADCState,
                             ZYNQ_XADC_FIFO_DEPTH),
        VMSTATE_UINT16(xadc_read_reg_previous, ZynqXADCState),
        VMSTATE_UINT16(xadc_dfifo_entries, ZynqXADCState),
        VMSTATE_END_OF_LIST()
    }
};

static void zynq_xadc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_zynq_xadc;
    dc->reset = zynq_xadc_reset;
}

static const TypeInfo zynq_xadc_info = {
    .class_init = zynq_xadc_class_init,
    .name  = TYPE_ZYNQ_XADC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(ZynqXADCState),
    .instance_init = zynq_xadc_init,
};

static void zynq_xadc_register_types(void)
{
    type_register_static(&zynq_xadc_info);
}

type_init(zynq_xadc_register_types)
