/*
 * QEMU ICH9 TCO emulation
 *
 * Copyright (c) 2015 Paulo Alcantara <pcacjr@zytor.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "qemu-common.h"
#include "sysemu/watchdog.h"
#include "hw/i386/ich9.h"

#include "hw/acpi/tco.h"

//#define DEBUG

#ifdef DEBUG
#define TCO_DEBUG(fmt, ...)                                     \
    do {                                                        \
        fprintf(stderr, "%s "fmt, __func__, ## __VA_ARGS__);    \
    } while (0)
#else
#define TCO_DEBUG(fmt, ...) do { } while (0)
#endif

enum {
    TCO_RLD_DEFAULT         = 0x0000,
    TCO_DAT_IN_DEFAULT      = 0x00,
    TCO_DAT_OUT_DEFAULT     = 0x00,
    TCO1_STS_DEFAULT        = 0x0000,
    TCO2_STS_DEFAULT        = 0x0000,
    TCO1_CNT_DEFAULT        = 0x0000,
    TCO2_CNT_DEFAULT        = 0x0008,
    TCO_MESSAGE1_DEFAULT    = 0x00,
    TCO_MESSAGE2_DEFAULT    = 0x00,
    TCO_WDCNT_DEFAULT       = 0x00,
    TCO_TMR_DEFAULT         = 0x0004,
    SW_IRQ_GEN_DEFAULT      = 0x03,
};

static inline void tco_timer_reload(TCOIORegs *tr)
{
    tr->expire_time = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
        ((int64_t)(tr->tco.tmr & TCO_TMR_MASK) * TCO_TICK_NSEC);
    timer_mod(tr->tco_timer, tr->expire_time);
}

static inline void tco_timer_stop(TCOIORegs *tr)
{
    tr->expire_time = -1;
}

static void tco_timer_expired(void *opaque)
{
    TCOIORegs *tr = opaque;
    ICH9LPCPMRegs *pm = container_of(tr, ICH9LPCPMRegs, tco_regs);
    ICH9LPCState *lpc = container_of(pm, ICH9LPCState, pm);
    uint32_t gcs = pci_get_long(lpc->chip_config + ICH9_CC_GCS);

    tr->tco.rld = 0;
    tr->tco.sts1 |= TCO_TIMEOUT;
    if (++tr->timeouts_no == 2) {
        tr->tco.sts2 |= TCO_SECOND_TO_STS;
        tr->tco.sts2 |= TCO_BOOT_STS;
        tr->timeouts_no = 0;

        if (!lpc->pin_strap.spkr_hi && !(gcs & ICH9_CC_GCS_NO_REBOOT)) {
            watchdog_perform_action();
            tco_timer_stop(tr);
            return;
        }
    }

    if (pm->smi_en & ICH9_PMIO_SMI_EN_TCO_EN) {
        ich9_generate_smi();
    } else {
        ich9_generate_nmi();
    }
    tr->tco.rld = tr->tco.tmr;
    tco_timer_reload(tr);
}

/* NOTE: values of 0 or 1 will be ignored by ICH */
static inline int can_start_tco_timer(TCOIORegs *tr)
{
    return !(tr->tco.cnt1 & TCO_TMR_HLT) && tr->tco.tmr > 1;
}

static uint32_t tco_ioport_readw(TCOIORegs *tr, uint32_t addr)
{
    uint16_t rld;

    switch (addr) {
    case TCO_RLD:
        if (tr->expire_time != -1) {
            int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
            int64_t elapsed = (tr->expire_time - now) / TCO_TICK_NSEC;
            rld = (uint16_t)elapsed | (tr->tco.rld & ~TCO_RLD_MASK);
        } else {
            rld = tr->tco.rld;
        }
        return rld;
    case TCO_DAT_IN:
        return tr->tco.din;
    case TCO_DAT_OUT:
        return tr->tco.dout;
    case TCO1_STS:
        return tr->tco.sts1;
    case TCO2_STS:
        return tr->tco.sts2;
    case TCO1_CNT:
        return tr->tco.cnt1;
    case TCO2_CNT:
        return tr->tco.cnt2;
    case TCO_MESSAGE1:
        return tr->tco.msg1;
    case TCO_MESSAGE2:
        return tr->tco.msg2;
    case TCO_WDCNT:
        return tr->tco.wdcnt;
    case TCO_TMR:
        return tr->tco.tmr;
    case SW_IRQ_GEN:
        return tr->sw_irq_gen;
    }
    return 0;
}

static void tco_ioport_writew(TCOIORegs *tr, uint32_t addr, uint32_t val)
{
    switch (addr) {
    case TCO_RLD:
        tr->timeouts_no = 0;
        if (can_start_tco_timer(tr)) {
            tr->tco.rld = tr->tco.tmr;
            tco_timer_reload(tr);
        } else {
            tr->tco.rld = val;
        }
        break;
    case TCO_DAT_IN:
        tr->tco.din = val;
        tr->tco.sts1 |= SW_TCO_SMI;
        ich9_generate_smi();
        break;
    case TCO_DAT_OUT:
        tr->tco.dout = val;
        tr->tco.sts1 |= TCO_INT_STS;
        /* TODO: cause an interrupt, as selected by the TCO_INT_SEL bits */
        break;
    case TCO1_STS:
        tr->tco.sts1 = val & TCO1_STS_MASK;
        break;
    case TCO2_STS:
        tr->tco.sts2 = val & TCO2_STS_MASK;
        break;
    case TCO1_CNT:
        val &= TCO1_CNT_MASK;
        /*
         * once TCO_LOCK bit is set, it can not be cleared by software. a reset
         * is required to change this bit from 1 to 0 -- it defaults to 0.
         */
        tr->tco.cnt1 = val | (tr->tco.cnt1 & TCO_LOCK);
        if (can_start_tco_timer(tr)) {
            tr->tco.rld = tr->tco.tmr;
            tco_timer_reload(tr);
        } else {
            tco_timer_stop(tr);
        }
        break;
    case TCO2_CNT:
        tr->tco.cnt2 = val;
        break;
    case TCO_MESSAGE1:
        tr->tco.msg1 = val;
        break;
    case TCO_MESSAGE2:
        tr->tco.msg2 = val;
        break;
    case TCO_WDCNT:
        tr->tco.wdcnt = val;
        break;
    case TCO_TMR:
        tr->tco.tmr = val;
        break;
    case SW_IRQ_GEN:
        tr->sw_irq_gen = val;
        break;
    }
}

static uint64_t tco_io_readw(void *opaque, hwaddr addr, unsigned width)
{
    TCOIORegs *tr = opaque;
    return tco_ioport_readw(tr, addr);
}

static void tco_io_writew(void *opaque, hwaddr addr, uint64_t val,
                          unsigned width)
{
    TCOIORegs *tr = opaque;
    tco_ioport_writew(tr, addr, val);
}

static const MemoryRegionOps tco_io_ops = {
    .read = tco_io_readw,
    .write = tco_io_writew,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .impl.min_access_size = 1,
    .impl.max_access_size = 2,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

void acpi_pm_tco_init(TCOIORegs *tr, MemoryRegion *parent)
{
    *tr = (TCOIORegs) {
        .tco = {
            .rld      = TCO_RLD_DEFAULT,
            .din      = TCO_DAT_IN_DEFAULT,
            .dout     = TCO_DAT_OUT_DEFAULT,
            .sts1     = TCO1_STS_DEFAULT,
            .sts2     = TCO2_STS_DEFAULT,
            .cnt1     = TCO1_CNT_DEFAULT,
            .cnt2     = TCO2_CNT_DEFAULT,
            .msg1     = TCO_MESSAGE1_DEFAULT,
            .msg2     = TCO_MESSAGE2_DEFAULT,
            .wdcnt    = TCO_WDCNT_DEFAULT,
            .tmr      = TCO_TMR_DEFAULT,
        },
        .sw_irq_gen    = SW_IRQ_GEN_DEFAULT,
        .tco_timer     = timer_new_ns(QEMU_CLOCK_VIRTUAL, tco_timer_expired, tr),
        .expire_time   = -1,
        .timeouts_no   = 0,
    };
    memory_region_init_io(&tr->io, memory_region_owner(parent),
                          &tco_io_ops, tr, "sm-tco", ICH9_PMIO_TCO_LEN);
    memory_region_add_subregion(parent, ICH9_PMIO_TCO_RLD, &tr->io);
}

const VMStateDescription vmstate_tco_io_sts = {
    .name = "tco io device status",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_UINT16(tco.rld, TCOIORegs),
        VMSTATE_UINT8(tco.din, TCOIORegs),
        VMSTATE_UINT8(tco.dout, TCOIORegs),
        VMSTATE_UINT16(tco.sts1, TCOIORegs),
        VMSTATE_UINT16(tco.sts2, TCOIORegs),
        VMSTATE_UINT16(tco.cnt1, TCOIORegs),
        VMSTATE_UINT16(tco.cnt2, TCOIORegs),
        VMSTATE_UINT8(tco.msg1, TCOIORegs),
        VMSTATE_UINT8(tco.msg2, TCOIORegs),
        VMSTATE_UINT8(tco.wdcnt, TCOIORegs),
        VMSTATE_UINT16(tco.tmr, TCOIORegs),
        VMSTATE_UINT8(sw_irq_gen, TCOIORegs),
        VMSTATE_TIMER_PTR(tco_timer, TCOIORegs),
        VMSTATE_INT64(expire_time, TCOIORegs),
        VMSTATE_UINT8(timeouts_no, TCOIORegs),
        VMSTATE_END_OF_LIST()
    }
};
