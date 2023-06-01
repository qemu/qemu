/*
 * QEMU Freescale eTSEC Emulator
 *
 * Copyright (c) 2011-2013 AdaCore
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/*
 * This implementation doesn't include ring priority, TCP/IP Off-Load, QoS.
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/net/mii.h"
#include "hw/ptimer.h"
#include "hw/qdev-properties.h"
#include "etsec.h"
#include "registers.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"

/* #define HEX_DUMP */
/* #define DEBUG_REGISTER */

#ifdef DEBUG_REGISTER
static const int debug_etsec = 1;
#else
static const int debug_etsec;
#endif

#define DPRINTF(fmt, ...) do {                 \
    if (debug_etsec) {                         \
        qemu_log(fmt , ## __VA_ARGS__);        \
    }                                          \
    } while (0)

/* call after any change to IEVENT or IMASK */
void etsec_update_irq(eTSEC *etsec)
{
    uint32_t ievent = etsec->regs[IEVENT].value;
    uint32_t imask  = etsec->regs[IMASK].value;
    uint32_t active = ievent & imask;

    int tx  = !!(active & IEVENT_TX_MASK);
    int rx  = !!(active & IEVENT_RX_MASK);
    int err = !!(active & IEVENT_ERR_MASK);

    DPRINTF("%s IRQ ievent=%"PRIx32" imask=%"PRIx32" %c%c%c",
            __func__, ievent, imask,
            tx  ? 'T' : '_',
            rx  ? 'R' : '_',
            err ? 'E' : '_');

    qemu_set_irq(etsec->tx_irq, tx);
    qemu_set_irq(etsec->rx_irq, rx);
    qemu_set_irq(etsec->err_irq, err);
}

static uint64_t etsec_read(void *opaque, hwaddr addr, unsigned size)
{
    eTSEC          *etsec     = opaque;
    uint32_t        reg_index = addr / 4;
    eTSEC_Register *reg       = NULL;
    uint32_t        ret       = 0x0;

    assert(reg_index < ETSEC_REG_NUMBER);

    reg = &etsec->regs[reg_index];


    switch (reg->access) {
    case ACC_WO:
        ret = 0x00000000;
        break;

    case ACC_RW:
    case ACC_W1C:
    case ACC_RO:
    default:
        ret = reg->value;
        break;
    }

    DPRINTF("Read  0x%08x @ 0x" HWADDR_FMT_plx
            "                            : %s (%s)\n",
            ret, addr, reg->name, reg->desc);

    return ret;
}

static void write_tstat(eTSEC          *etsec,
                        eTSEC_Register *reg,
                        uint32_t        reg_index,
                        uint32_t        value)
{
    int i = 0;

    for (i = 0; i < 8; i++) {
        /* Check THLTi flag in TSTAT */
        if (value & (1 << (31 - i))) {
            etsec_walk_tx_ring(etsec, i);
        }
    }

    /* Write 1 to clear */
    reg->value &= ~value;
}

static void write_rstat(eTSEC          *etsec,
                        eTSEC_Register *reg,
                        uint32_t        reg_index,
                        uint32_t        value)
{
    int i = 0;

    for (i = 0; i < 8; i++) {
        /* Check QHLTi flag in RSTAT */
        if (value & (1 << (23 - i)) && !(reg->value & (1 << (23 - i)))) {
            etsec_walk_rx_ring(etsec, i);
        }
    }

    /* Write 1 to clear */
    reg->value &= ~value;
}

static void write_tbasex(eTSEC          *etsec,
                         eTSEC_Register *reg,
                         uint32_t        reg_index,
                         uint32_t        value)
{
    reg->value = value & ~0x7;

    /* Copy this value in the ring's TxBD pointer */
    etsec->regs[TBPTR0 + (reg_index - TBASE0)].value = value & ~0x7;
}

static void write_rbasex(eTSEC          *etsec,
                         eTSEC_Register *reg,
                         uint32_t        reg_index,
                         uint32_t        value)
{
    reg->value = value & ~0x7;

    /* Copy this value in the ring's RxBD pointer */
    etsec->regs[RBPTR0 + (reg_index - RBASE0)].value = value & ~0x7;
}

static void write_dmactrl(eTSEC          *etsec,
                          eTSEC_Register *reg,
                          uint32_t        reg_index,
                          uint32_t        value)
{
    reg->value = value;

    if (value & DMACTRL_GRS) {

        if (etsec->rx_buffer_len != 0) {
            /* Graceful receive stop delayed until end of frame */
        } else {
            /* Graceful receive stop now */
            etsec->regs[IEVENT].value |= IEVENT_GRSC;
            etsec_update_irq(etsec);
        }
    }

    if (value & DMACTRL_GTS) {

        if (etsec->tx_buffer_len != 0) {
            /* Graceful transmit stop delayed until end of frame */
        } else {
            /* Graceful transmit stop now */
            etsec->regs[IEVENT].value |= IEVENT_GTSC;
            etsec_update_irq(etsec);
        }
    }

    if (!(value & DMACTRL_WOP)) {
        /* Start polling */
        ptimer_transaction_begin(etsec->ptimer);
        ptimer_stop(etsec->ptimer);
        ptimer_set_count(etsec->ptimer, 1);
        ptimer_run(etsec->ptimer, 1);
        ptimer_transaction_commit(etsec->ptimer);
    }
}

static void etsec_write(void     *opaque,
                        hwaddr    addr,
                        uint64_t  value,
                        unsigned  size)
{
    eTSEC          *etsec     = opaque;
    uint32_t        reg_index = addr / 4;
    eTSEC_Register *reg       = NULL;
    uint32_t        before    = 0x0;

    assert(reg_index < ETSEC_REG_NUMBER);

    reg = &etsec->regs[reg_index];
    before = reg->value;

    switch (reg_index) {
    case IEVENT:
        /* Write 1 to clear */
        reg->value &= ~value;

        etsec_update_irq(etsec);
        break;

    case IMASK:
        reg->value = value;

        etsec_update_irq(etsec);
        break;

    case DMACTRL:
        write_dmactrl(etsec, reg, reg_index, value);
        break;

    case TSTAT:
        write_tstat(etsec, reg, reg_index, value);
        break;

    case RSTAT:
        write_rstat(etsec, reg, reg_index, value);
        break;

    case TBASE0 ... TBASE7:
        write_tbasex(etsec, reg, reg_index, value);
        break;

    case RBASE0 ... RBASE7:
        write_rbasex(etsec, reg, reg_index, value);
        break;

    case MIIMCFG ... MIIMIND:
        etsec_write_miim(etsec, reg, reg_index, value);
        break;

    default:
        /* Default handling */
        switch (reg->access) {

        case ACC_RW:
        case ACC_WO:
            reg->value = value;
            break;

        case ACC_W1C:
            reg->value &= ~value;
            break;

        case ACC_RO:
        default:
            /* Read Only or Unknown register */
            break;
        }
    }

    DPRINTF("Write 0x%08x @ 0x" HWADDR_FMT_plx
            " val:0x%08x->0x%08x : %s (%s)\n",
            (unsigned int)value, addr, before, reg->value,
            reg->name, reg->desc);
}

static const MemoryRegionOps etsec_ops = {
    .read = etsec_read,
    .write = etsec_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void etsec_timer_hit(void *opaque)
{
    eTSEC *etsec = opaque;

    ptimer_stop(etsec->ptimer);

    if (!(etsec->regs[DMACTRL].value & DMACTRL_WOP)) {

        if (!(etsec->regs[DMACTRL].value & DMACTRL_GTS)) {
            etsec_walk_tx_ring(etsec, 0);
        }
        ptimer_set_count(etsec->ptimer, 1);
        ptimer_run(etsec->ptimer, 1);
    }
}

static void etsec_reset(DeviceState *d)
{
    eTSEC *etsec = ETSEC_COMMON(d);
    int i = 0;
    int reg_index = 0;

    /* Default value for all registers */
    for (i = 0; i < ETSEC_REG_NUMBER; i++) {
        etsec->regs[i].name   = "Reserved";
        etsec->regs[i].desc   = "";
        etsec->regs[i].access = ACC_UNKNOWN;
        etsec->regs[i].value  = 0x00000000;
    }

    /* Set-up known registers */
    for (i = 0; eTSEC_registers_def[i].name != NULL; i++) {

        reg_index = eTSEC_registers_def[i].offset / 4;

        etsec->regs[reg_index].name   = eTSEC_registers_def[i].name;
        etsec->regs[reg_index].desc   = eTSEC_registers_def[i].desc;
        etsec->regs[reg_index].access = eTSEC_registers_def[i].access;
        etsec->regs[reg_index].value  = eTSEC_registers_def[i].reset;
    }

    etsec->tx_buffer     = NULL;
    etsec->tx_buffer_len = 0;
    etsec->rx_buffer     = NULL;
    etsec->rx_buffer_len = 0;

    etsec->phy_status =
        MII_BMSR_EXTCAP   | MII_BMSR_LINK_ST  | MII_BMSR_AUTONEG  |
        MII_BMSR_AN_COMP  | MII_BMSR_MFPS     | MII_BMSR_EXTSTAT  |
        MII_BMSR_100T2_HD | MII_BMSR_100T2_FD |
        MII_BMSR_10T_HD   | MII_BMSR_10T_FD   |
        MII_BMSR_100TX_HD | MII_BMSR_100TX_FD | MII_BMSR_100T4;

    etsec_update_irq(etsec);
}

static ssize_t etsec_receive(NetClientState *nc,
                             const uint8_t  *buf,
                             size_t          size)
{
    ssize_t ret;
    eTSEC *etsec = qemu_get_nic_opaque(nc);

#if defined(HEX_DUMP)
    fprintf(stderr, "%s receive size:%zd\n", nc->name, size);
    qemu_hexdump(stderr, "", buf, size);
#endif
    /* Flush is unnecessary as are already in receiving path */
    etsec->need_flush = false;
    ret = etsec_rx_ring_write(etsec, buf, size);
    if (ret == 0) {
        /* The packet will be queued, let's flush it when buffer is available
         * again. */
        etsec->need_flush = true;
    }
    return ret;
}


static void etsec_set_link_status(NetClientState *nc)
{
    eTSEC *etsec = qemu_get_nic_opaque(nc);

    etsec_miim_link_status(etsec, nc);
}

static NetClientInfo net_etsec_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .receive = etsec_receive,
    .link_status_changed = etsec_set_link_status,
};

static void etsec_realize(DeviceState *dev, Error **errp)
{
    eTSEC        *etsec = ETSEC_COMMON(dev);

    etsec->nic = qemu_new_nic(&net_etsec_info, &etsec->conf,
                              object_get_typename(OBJECT(dev)), dev->id,
                              &dev->mem_reentrancy_guard, etsec);
    qemu_format_nic_info_str(qemu_get_queue(etsec->nic), etsec->conf.macaddr.a);

    etsec->ptimer = ptimer_init(etsec_timer_hit, etsec, PTIMER_POLICY_LEGACY);
    ptimer_transaction_begin(etsec->ptimer);
    ptimer_set_freq(etsec->ptimer, 100);
    ptimer_transaction_commit(etsec->ptimer);
}

static void etsec_instance_init(Object *obj)
{
    eTSEC        *etsec = ETSEC_COMMON(obj);
    SysBusDevice *sbd   = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&etsec->io_area, OBJECT(etsec), &etsec_ops, etsec,
                          "eTSEC", 0x1000);
    sysbus_init_mmio(sbd, &etsec->io_area);

    sysbus_init_irq(sbd, &etsec->tx_irq);
    sysbus_init_irq(sbd, &etsec->rx_irq);
    sysbus_init_irq(sbd, &etsec->err_irq);
}

static Property etsec_properties[] = {
    DEFINE_NIC_PROPERTIES(eTSEC, conf),
    DEFINE_PROP_END_OF_LIST(),
};

static void etsec_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = etsec_realize;
    dc->reset = etsec_reset;
    device_class_set_props(dc, etsec_properties);
    /* Supported by ppce500 machine */
    dc->user_creatable = true;
}

static const TypeInfo etsec_info = {
    .name                  = TYPE_ETSEC_COMMON,
    .parent                = TYPE_SYS_BUS_DEVICE,
    .instance_size         = sizeof(eTSEC),
    .class_init            = etsec_class_init,
    .instance_init         = etsec_instance_init,
};

static void etsec_register_types(void)
{
    type_register_static(&etsec_info);
}

type_init(etsec_register_types)
