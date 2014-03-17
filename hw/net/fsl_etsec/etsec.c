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

#include "sysemu/sysemu.h"
#include "hw/sysbus.h"
#include "trace.h"
#include "hw/ptimer.h"
#include "etsec.h"
#include "registers.h"

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

    DPRINTF("Read  0x%08x @ 0x" TARGET_FMT_plx
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

static void write_ievent(eTSEC          *etsec,
                         eTSEC_Register *reg,
                         uint32_t        reg_index,
                         uint32_t        value)
{
    /* Write 1 to clear */
    reg->value &= ~value;

    if (!(reg->value & (IEVENT_TXF | IEVENT_TXF))) {
        qemu_irq_lower(etsec->tx_irq);
    }
    if (!(reg->value & (IEVENT_RXF | IEVENT_RXF))) {
        qemu_irq_lower(etsec->rx_irq);
    }

    if (!(reg->value & (IEVENT_MAG | IEVENT_GTSC | IEVENT_GRSC | IEVENT_TXC |
                        IEVENT_RXC | IEVENT_BABR | IEVENT_BABT | IEVENT_LC |
                        IEVENT_CRL | IEVENT_FGPI | IEVENT_FIR | IEVENT_FIQ |
                        IEVENT_DPE | IEVENT_PERR | IEVENT_EBERR | IEVENT_TXE |
                        IEVENT_XFUN | IEVENT_BSY | IEVENT_MSRO | IEVENT_MMRD |
                        IEVENT_MMRW))) {
        qemu_irq_lower(etsec->err_irq);
    }
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
            if (etsec->regs[IMASK].value & IMASK_GRSCEN) {
                qemu_irq_raise(etsec->err_irq);
            }
        }
    }

    if (value & DMACTRL_GTS) {

        if (etsec->tx_buffer_len != 0) {
            /* Graceful transmit stop delayed until end of frame */
        } else {
            /* Graceful transmit stop now */
            etsec->regs[IEVENT].value |= IEVENT_GTSC;
            if (etsec->regs[IMASK].value & IMASK_GTSCEN) {
                qemu_irq_raise(etsec->err_irq);
            }
        }
    }

    if (!(value & DMACTRL_WOP)) {
        /* Start polling */
        ptimer_stop(etsec->ptimer);
        ptimer_set_count(etsec->ptimer, 1);
        ptimer_run(etsec->ptimer, 1);
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
        write_ievent(etsec, reg, reg_index, value);
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

    DPRINTF("Write 0x%08x @ 0x" TARGET_FMT_plx
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
        MII_SR_EXTENDED_CAPS    | MII_SR_LINK_STATUS   | MII_SR_AUTONEG_CAPS  |
        MII_SR_AUTONEG_COMPLETE | MII_SR_PREAMBLE_SUPPRESS |
        MII_SR_EXTENDED_STATUS  | MII_SR_100T2_HD_CAPS | MII_SR_100T2_FD_CAPS |
        MII_SR_10T_HD_CAPS      | MII_SR_10T_FD_CAPS   | MII_SR_100X_HD_CAPS  |
        MII_SR_100X_FD_CAPS     | MII_SR_100T4_CAPS;
}

static void etsec_cleanup(NetClientState *nc)
{
    /* qemu_log("eTSEC cleanup\n"); */
}

static int etsec_can_receive(NetClientState *nc)
{
    eTSEC *etsec = qemu_get_nic_opaque(nc);

    return etsec->rx_buffer_len == 0;
}

static ssize_t etsec_receive(NetClientState *nc,
                             const uint8_t  *buf,
                             size_t          size)
{
    eTSEC *etsec = qemu_get_nic_opaque(nc);

#if defined(HEX_DUMP)
    fprintf(stderr, "%s receive size:%d\n", etsec->nic->nc.name, size);
    qemu_hexdump(buf, stderr, "", size);
#endif
    etsec_rx_ring_write(etsec, buf, size);
    return size;
}


static void etsec_set_link_status(NetClientState *nc)
{
    eTSEC *etsec = qemu_get_nic_opaque(nc);

    etsec_miim_link_status(etsec, nc);
}

static NetClientInfo net_etsec_info = {
    .type = NET_CLIENT_OPTIONS_KIND_NIC,
    .size = sizeof(NICState),
    .can_receive = etsec_can_receive,
    .receive = etsec_receive,
    .cleanup = etsec_cleanup,
    .link_status_changed = etsec_set_link_status,
};

static void etsec_realize(DeviceState *dev, Error **errp)
{
    eTSEC        *etsec = ETSEC_COMMON(dev);

    etsec->nic = qemu_new_nic(&net_etsec_info, &etsec->conf,
                              object_get_typename(OBJECT(dev)), dev->id, etsec);
    qemu_format_nic_info_str(qemu_get_queue(etsec->nic), etsec->conf.macaddr.a);


    etsec->bh     = qemu_bh_new(etsec_timer_hit, etsec);
    etsec->ptimer = ptimer_init(etsec->bh);
    ptimer_set_freq(etsec->ptimer, 100);
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
    dc->props = etsec_properties;
}

static TypeInfo etsec_info = {
    .name                  = "eTSEC",
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

DeviceState *etsec_create(hwaddr         base,
                          MemoryRegion * mr,
                          NICInfo      * nd,
                          qemu_irq       tx_irq,
                          qemu_irq       rx_irq,
                          qemu_irq       err_irq)
{
    DeviceState *dev;

    dev = qdev_create(NULL, "eTSEC");
    qdev_set_nic_properties(dev, nd);

    if (qdev_init(dev)) {
        return NULL;
    }

    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, tx_irq);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 1, rx_irq);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 2, err_irq);

    memory_region_add_subregion(mr, base,
                                SYS_BUS_DEVICE(dev)->mmio[0].memory);

    return dev;
}
