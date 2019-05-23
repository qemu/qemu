/*
 * Arm IoT Kit security controller
 *
 * Copyright (c) 2018 Linaro Limited
 * Written by Peter Maydell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "trace.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "hw/misc/iotkit-secctl.h"

/* Registers in the secure privilege control block */
REG32(SECRESPCFG, 0x10)
REG32(NSCCFG, 0x14)
REG32(SECMPCINTSTATUS, 0x1c)
REG32(SECPPCINTSTAT, 0x20)
REG32(SECPPCINTCLR, 0x24)
REG32(SECPPCINTEN, 0x28)
REG32(SECMSCINTSTAT, 0x30)
REG32(SECMSCINTCLR, 0x34)
REG32(SECMSCINTEN, 0x38)
REG32(BRGINTSTAT, 0x40)
REG32(BRGINTCLR, 0x44)
REG32(BRGINTEN, 0x48)
REG32(AHBNSPPC0, 0x50)
REG32(AHBNSPPCEXP0, 0x60)
REG32(AHBNSPPCEXP1, 0x64)
REG32(AHBNSPPCEXP2, 0x68)
REG32(AHBNSPPCEXP3, 0x6c)
REG32(APBNSPPC0, 0x70)
REG32(APBNSPPC1, 0x74)
REG32(APBNSPPCEXP0, 0x80)
REG32(APBNSPPCEXP1, 0x84)
REG32(APBNSPPCEXP2, 0x88)
REG32(APBNSPPCEXP3, 0x8c)
REG32(AHBSPPPC0, 0x90)
REG32(AHBSPPPCEXP0, 0xa0)
REG32(AHBSPPPCEXP1, 0xa4)
REG32(AHBSPPPCEXP2, 0xa8)
REG32(AHBSPPPCEXP3, 0xac)
REG32(APBSPPPC0, 0xb0)
REG32(APBSPPPC1, 0xb4)
REG32(APBSPPPCEXP0, 0xc0)
REG32(APBSPPPCEXP1, 0xc4)
REG32(APBSPPPCEXP2, 0xc8)
REG32(APBSPPPCEXP3, 0xcc)
REG32(NSMSCEXP, 0xd0)
REG32(PID4, 0xfd0)
REG32(PID5, 0xfd4)
REG32(PID6, 0xfd8)
REG32(PID7, 0xfdc)
REG32(PID0, 0xfe0)
REG32(PID1, 0xfe4)
REG32(PID2, 0xfe8)
REG32(PID3, 0xfec)
REG32(CID0, 0xff0)
REG32(CID1, 0xff4)
REG32(CID2, 0xff8)
REG32(CID3, 0xffc)

/* Registers in the non-secure privilege control block */
REG32(AHBNSPPPC0, 0x90)
REG32(AHBNSPPPCEXP0, 0xa0)
REG32(AHBNSPPPCEXP1, 0xa4)
REG32(AHBNSPPPCEXP2, 0xa8)
REG32(AHBNSPPPCEXP3, 0xac)
REG32(APBNSPPPC0, 0xb0)
REG32(APBNSPPPC1, 0xb4)
REG32(APBNSPPPCEXP0, 0xc0)
REG32(APBNSPPPCEXP1, 0xc4)
REG32(APBNSPPPCEXP2, 0xc8)
REG32(APBNSPPPCEXP3, 0xcc)
/* PID and CID registers are also present in the NS block */

static const uint8_t iotkit_secctl_s_idregs[] = {
    0x04, 0x00, 0x00, 0x00,
    0x52, 0xb8, 0x0b, 0x00,
    0x0d, 0xf0, 0x05, 0xb1,
};

static const uint8_t iotkit_secctl_ns_idregs[] = {
    0x04, 0x00, 0x00, 0x00,
    0x53, 0xb8, 0x0b, 0x00,
    0x0d, 0xf0, 0x05, 0xb1,
};

/* The register sets for the various PPCs (AHB internal, APB internal,
 * AHB expansion, APB expansion) are all set up so that they are
 * in 16-aligned blocks so offsets 0xN0, 0xN4, 0xN8, 0xNC are PPCs
 * 0, 1, 2, 3 of that type, so we can convert a register address offset
 * into an an index into a PPC array easily.
 */
static inline int offset_to_ppc_idx(uint32_t offset)
{
    return extract32(offset, 2, 2);
}

typedef void PerPPCFunction(IoTKitSecCtlPPC *ppc);

static void foreach_ppc(IoTKitSecCtl *s, PerPPCFunction *fn)
{
    int i;

    for (i = 0; i < IOTS_NUM_APB_PPC; i++) {
        fn(&s->apb[i]);
    }
    for (i = 0; i < IOTS_NUM_APB_EXP_PPC; i++) {
        fn(&s->apbexp[i]);
    }
    for (i = 0; i < IOTS_NUM_AHB_EXP_PPC; i++) {
        fn(&s->ahbexp[i]);
    }
}

static MemTxResult iotkit_secctl_s_read(void *opaque, hwaddr addr,
                                        uint64_t *pdata,
                                        unsigned size, MemTxAttrs attrs)
{
    uint64_t r;
    uint32_t offset = addr & ~0x3;
    IoTKitSecCtl *s = IOTKIT_SECCTL(opaque);

    switch (offset) {
    case A_AHBNSPPC0:
    case A_AHBSPPPC0:
        r = 0;
        break;
    case A_SECRESPCFG:
        r = s->secrespcfg;
        break;
    case A_NSCCFG:
        r = s->nsccfg;
        break;
    case A_SECMPCINTSTATUS:
        r = s->mpcintstatus;
        break;
    case A_SECPPCINTSTAT:
        r = s->secppcintstat;
        break;
    case A_SECPPCINTEN:
        r = s->secppcinten;
        break;
    case A_BRGINTSTAT:
        /* QEMU's bus fabric can never report errors as it doesn't buffer
         * writes, so we never report bridge interrupts.
         */
        r = 0;
        break;
    case A_BRGINTEN:
        r = s->brginten;
        break;
    case A_AHBNSPPCEXP0:
    case A_AHBNSPPCEXP1:
    case A_AHBNSPPCEXP2:
    case A_AHBNSPPCEXP3:
        r = s->ahbexp[offset_to_ppc_idx(offset)].ns;
        break;
    case A_APBNSPPC0:
    case A_APBNSPPC1:
        r = s->apb[offset_to_ppc_idx(offset)].ns;
        break;
    case A_APBNSPPCEXP0:
    case A_APBNSPPCEXP1:
    case A_APBNSPPCEXP2:
    case A_APBNSPPCEXP3:
        r = s->apbexp[offset_to_ppc_idx(offset)].ns;
        break;
    case A_AHBSPPPCEXP0:
    case A_AHBSPPPCEXP1:
    case A_AHBSPPPCEXP2:
    case A_AHBSPPPCEXP3:
        r = s->apbexp[offset_to_ppc_idx(offset)].sp;
        break;
    case A_APBSPPPC0:
    case A_APBSPPPC1:
        r = s->apb[offset_to_ppc_idx(offset)].sp;
        break;
    case A_APBSPPPCEXP0:
    case A_APBSPPPCEXP1:
    case A_APBSPPPCEXP2:
    case A_APBSPPPCEXP3:
        r = s->apbexp[offset_to_ppc_idx(offset)].sp;
        break;
    case A_SECMSCINTSTAT:
        r = s->secmscintstat;
        break;
    case A_SECMSCINTEN:
        r = s->secmscinten;
        break;
    case A_NSMSCEXP:
        r = s->nsmscexp;
        break;
    case A_PID4:
    case A_PID5:
    case A_PID6:
    case A_PID7:
    case A_PID0:
    case A_PID1:
    case A_PID2:
    case A_PID3:
    case A_CID0:
    case A_CID1:
    case A_CID2:
    case A_CID3:
        r = iotkit_secctl_s_idregs[(offset - A_PID4) / 4];
        break;
    case A_SECPPCINTCLR:
    case A_SECMSCINTCLR:
    case A_BRGINTCLR:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "IotKit SecCtl S block read: write-only offset 0x%x\n",
                      offset);
        r = 0;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "IotKit SecCtl S block read: bad offset 0x%x\n", offset);
        r = 0;
        break;
    }

    if (size != 4) {
        /* None of our registers are access-sensitive, so just pull the right
         * byte out of the word read result.
         */
        r = extract32(r, (addr & 3) * 8, size * 8);
    }

    trace_iotkit_secctl_s_read(offset, r, size);
    *pdata = r;
    return MEMTX_OK;
}

static void iotkit_secctl_update_ppc_ap(IoTKitSecCtlPPC *ppc)
{
    int i;

    for (i = 0; i < ppc->numports; i++) {
        bool v;

        if (extract32(ppc->ns, i, 1)) {
            v = extract32(ppc->nsp, i, 1);
        } else {
            v = extract32(ppc->sp, i, 1);
        }
        qemu_set_irq(ppc->ap[i], v);
    }
}

static void iotkit_secctl_ppc_ns_write(IoTKitSecCtlPPC *ppc, uint32_t value)
{
    int i;

    ppc->ns = value & MAKE_64BIT_MASK(0, ppc->numports);
    for (i = 0; i < ppc->numports; i++) {
        qemu_set_irq(ppc->nonsec[i], extract32(ppc->ns, i, 1));
    }
    iotkit_secctl_update_ppc_ap(ppc);
}

static void iotkit_secctl_ppc_sp_write(IoTKitSecCtlPPC *ppc, uint32_t value)
{
    ppc->sp = value & MAKE_64BIT_MASK(0, ppc->numports);
    iotkit_secctl_update_ppc_ap(ppc);
}

static void iotkit_secctl_ppc_nsp_write(IoTKitSecCtlPPC *ppc, uint32_t value)
{
    ppc->nsp = value & MAKE_64BIT_MASK(0, ppc->numports);
    iotkit_secctl_update_ppc_ap(ppc);
}

static void iotkit_secctl_ppc_update_irq_clear(IoTKitSecCtlPPC *ppc)
{
    uint32_t value = ppc->parent->secppcintstat;

    qemu_set_irq(ppc->irq_clear, extract32(value, ppc->irq_bit_offset, 1));
}

static void iotkit_secctl_ppc_update_irq_enable(IoTKitSecCtlPPC *ppc)
{
    uint32_t value = ppc->parent->secppcinten;

    qemu_set_irq(ppc->irq_enable, extract32(value, ppc->irq_bit_offset, 1));
}

static void iotkit_secctl_update_mscexp_irqs(qemu_irq *msc_irqs, uint32_t value)
{
    int i;

    for (i = 0; i < IOTS_NUM_EXP_MSC; i++) {
        qemu_set_irq(msc_irqs[i], extract32(value, i + 16, 1));
    }
}

static void iotkit_secctl_update_msc_irq(IoTKitSecCtl *s)
{
    /* Update the combined MSC IRQ, based on S_MSCEXP_STATUS and S_MSCEXP_EN */
    bool level = s->secmscintstat & s->secmscinten;

    qemu_set_irq(s->msc_irq, level);
}

static MemTxResult iotkit_secctl_s_write(void *opaque, hwaddr addr,
                                         uint64_t value,
                                         unsigned size, MemTxAttrs attrs)
{
    IoTKitSecCtl *s = IOTKIT_SECCTL(opaque);
    uint32_t offset = addr;
    IoTKitSecCtlPPC *ppc;

    trace_iotkit_secctl_s_write(offset, value, size);

    if (size != 4) {
        /* Byte and halfword writes are ignored */
        qemu_log_mask(LOG_GUEST_ERROR,
                      "IotKit SecCtl S block write: bad size, ignored\n");
        return MEMTX_OK;
    }

    switch (offset) {
    case A_NSCCFG:
        s->nsccfg = value & 3;
        qemu_set_irq(s->nsc_cfg_irq, s->nsccfg);
        break;
    case A_SECRESPCFG:
        value &= 1;
        s->secrespcfg = value;
        qemu_set_irq(s->sec_resp_cfg, s->secrespcfg);
        break;
    case A_SECPPCINTCLR:
        value &= 0x00f000f3;
        foreach_ppc(s, iotkit_secctl_ppc_update_irq_clear);
        break;
    case A_SECPPCINTEN:
        s->secppcinten = value & 0x00f000f3;
        foreach_ppc(s, iotkit_secctl_ppc_update_irq_enable);
        break;
    case A_BRGINTCLR:
        break;
    case A_BRGINTEN:
        s->brginten = value & 0xffff0000;
        break;
    case A_AHBNSPPCEXP0:
    case A_AHBNSPPCEXP1:
    case A_AHBNSPPCEXP2:
    case A_AHBNSPPCEXP3:
        ppc = &s->ahbexp[offset_to_ppc_idx(offset)];
        iotkit_secctl_ppc_ns_write(ppc, value);
        break;
    case A_APBNSPPC0:
    case A_APBNSPPC1:
        ppc = &s->apb[offset_to_ppc_idx(offset)];
        iotkit_secctl_ppc_ns_write(ppc, value);
        break;
    case A_APBNSPPCEXP0:
    case A_APBNSPPCEXP1:
    case A_APBNSPPCEXP2:
    case A_APBNSPPCEXP3:
        ppc = &s->apbexp[offset_to_ppc_idx(offset)];
        iotkit_secctl_ppc_ns_write(ppc, value);
        break;
    case A_AHBSPPPCEXP0:
    case A_AHBSPPPCEXP1:
    case A_AHBSPPPCEXP2:
    case A_AHBSPPPCEXP3:
        ppc = &s->ahbexp[offset_to_ppc_idx(offset)];
        iotkit_secctl_ppc_sp_write(ppc, value);
        break;
    case A_APBSPPPC0:
    case A_APBSPPPC1:
        ppc = &s->apb[offset_to_ppc_idx(offset)];
        iotkit_secctl_ppc_sp_write(ppc, value);
        break;
    case A_APBSPPPCEXP0:
    case A_APBSPPPCEXP1:
    case A_APBSPPPCEXP2:
    case A_APBSPPPCEXP3:
        ppc = &s->apbexp[offset_to_ppc_idx(offset)];
        iotkit_secctl_ppc_sp_write(ppc, value);
        break;
    case A_SECMSCINTCLR:
        iotkit_secctl_update_mscexp_irqs(s->mscexp_clear, value);
        break;
    case A_SECMSCINTEN:
        s->secmscinten = value;
        iotkit_secctl_update_msc_irq(s);
        break;
    case A_NSMSCEXP:
        s->nsmscexp = value;
        iotkit_secctl_update_mscexp_irqs(s->mscexp_ns, value);
        break;
    case A_SECMPCINTSTATUS:
    case A_SECPPCINTSTAT:
    case A_SECMSCINTSTAT:
    case A_BRGINTSTAT:
    case A_AHBNSPPC0:
    case A_AHBSPPPC0:
    case A_PID4:
    case A_PID5:
    case A_PID6:
    case A_PID7:
    case A_PID0:
    case A_PID1:
    case A_PID2:
    case A_PID3:
    case A_CID0:
    case A_CID1:
    case A_CID2:
    case A_CID3:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "IoTKit SecCtl S block write: "
                      "read-only offset 0x%x\n", offset);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "IotKit SecCtl S block write: bad offset 0x%x\n",
                      offset);
        break;
    }

    return MEMTX_OK;
}

static MemTxResult iotkit_secctl_ns_read(void *opaque, hwaddr addr,
                                         uint64_t *pdata,
                                         unsigned size, MemTxAttrs attrs)
{
    IoTKitSecCtl *s = IOTKIT_SECCTL(opaque);
    uint64_t r;
    uint32_t offset = addr & ~0x3;

    switch (offset) {
    case A_AHBNSPPPC0:
        r = 0;
        break;
    case A_AHBNSPPPCEXP0:
    case A_AHBNSPPPCEXP1:
    case A_AHBNSPPPCEXP2:
    case A_AHBNSPPPCEXP3:
        r = s->ahbexp[offset_to_ppc_idx(offset)].nsp;
        break;
    case A_APBNSPPPC0:
    case A_APBNSPPPC1:
        r = s->apb[offset_to_ppc_idx(offset)].nsp;
        break;
    case A_APBNSPPPCEXP0:
    case A_APBNSPPPCEXP1:
    case A_APBNSPPPCEXP2:
    case A_APBNSPPPCEXP3:
        r = s->apbexp[offset_to_ppc_idx(offset)].nsp;
        break;
    case A_PID4:
    case A_PID5:
    case A_PID6:
    case A_PID7:
    case A_PID0:
    case A_PID1:
    case A_PID2:
    case A_PID3:
    case A_CID0:
    case A_CID1:
    case A_CID2:
    case A_CID3:
        r = iotkit_secctl_ns_idregs[(offset - A_PID4) / 4];
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "IotKit SecCtl NS block write: bad offset 0x%x\n",
                      offset);
        r = 0;
        break;
    }

    if (size != 4) {
        /* None of our registers are access-sensitive, so just pull the right
         * byte out of the word read result.
         */
        r = extract32(r, (addr & 3) * 8, size * 8);
    }

    trace_iotkit_secctl_ns_read(offset, r, size);
    *pdata = r;
    return MEMTX_OK;
}

static MemTxResult iotkit_secctl_ns_write(void *opaque, hwaddr addr,
                                          uint64_t value,
                                          unsigned size, MemTxAttrs attrs)
{
    IoTKitSecCtl *s = IOTKIT_SECCTL(opaque);
    uint32_t offset = addr;
    IoTKitSecCtlPPC *ppc;

    trace_iotkit_secctl_ns_write(offset, value, size);

    if (size != 4) {
        /* Byte and halfword writes are ignored */
        qemu_log_mask(LOG_GUEST_ERROR,
                      "IotKit SecCtl NS block write: bad size, ignored\n");
        return MEMTX_OK;
    }

    switch (offset) {
    case A_AHBNSPPPCEXP0:
    case A_AHBNSPPPCEXP1:
    case A_AHBNSPPPCEXP2:
    case A_AHBNSPPPCEXP3:
        ppc = &s->ahbexp[offset_to_ppc_idx(offset)];
        iotkit_secctl_ppc_nsp_write(ppc, value);
        break;
    case A_APBNSPPPC0:
    case A_APBNSPPPC1:
        ppc = &s->apb[offset_to_ppc_idx(offset)];
        iotkit_secctl_ppc_nsp_write(ppc, value);
        break;
    case A_APBNSPPPCEXP0:
    case A_APBNSPPPCEXP1:
    case A_APBNSPPPCEXP2:
    case A_APBNSPPPCEXP3:
        ppc = &s->apbexp[offset_to_ppc_idx(offset)];
        iotkit_secctl_ppc_nsp_write(ppc, value);
        break;
    case A_AHBNSPPPC0:
    case A_PID4:
    case A_PID5:
    case A_PID6:
    case A_PID7:
    case A_PID0:
    case A_PID1:
    case A_PID2:
    case A_PID3:
    case A_CID0:
    case A_CID1:
    case A_CID2:
    case A_CID3:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "IoTKit SecCtl NS block write: "
                      "read-only offset 0x%x\n", offset);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "IotKit SecCtl NS block write: bad offset 0x%x\n",
                      offset);
        break;
    }

    return MEMTX_OK;
}

static const MemoryRegionOps iotkit_secctl_s_ops = {
    .read_with_attrs = iotkit_secctl_s_read,
    .write_with_attrs = iotkit_secctl_s_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .impl.min_access_size = 1,
    .impl.max_access_size = 4,
};

static const MemoryRegionOps iotkit_secctl_ns_ops = {
    .read_with_attrs = iotkit_secctl_ns_read,
    .write_with_attrs = iotkit_secctl_ns_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .impl.min_access_size = 1,
    .impl.max_access_size = 4,
};

static void iotkit_secctl_reset_ppc(IoTKitSecCtlPPC *ppc)
{
    ppc->ns = 0;
    ppc->sp = 0;
    ppc->nsp = 0;
}

static void iotkit_secctl_reset(DeviceState *dev)
{
    IoTKitSecCtl *s = IOTKIT_SECCTL(dev);

    s->secppcintstat = 0;
    s->secppcinten = 0;
    s->secrespcfg = 0;
    s->nsccfg = 0;
    s->brginten = 0;

    foreach_ppc(s, iotkit_secctl_reset_ppc);
}

static void iotkit_secctl_mpc_status(void *opaque, int n, int level)
{
    IoTKitSecCtl *s = IOTKIT_SECCTL(opaque);

    s->mpcintstatus = deposit32(s->mpcintstatus, n, 1, !!level);
}

static void iotkit_secctl_mpcexp_status(void *opaque, int n, int level)
{
    IoTKitSecCtl *s = IOTKIT_SECCTL(opaque);

    s->mpcintstatus = deposit32(s->mpcintstatus, n + 16, 1, !!level);
}

static void iotkit_secctl_mscexp_status(void *opaque, int n, int level)
{
    IoTKitSecCtl *s = IOTKIT_SECCTL(opaque);

    s->secmscintstat = deposit32(s->secmscintstat, n + 16, 1, !!level);
    iotkit_secctl_update_msc_irq(s);
}

static void iotkit_secctl_ppc_irqstatus(void *opaque, int n, int level)
{
    IoTKitSecCtlPPC *ppc = opaque;
    IoTKitSecCtl *s = IOTKIT_SECCTL(ppc->parent);
    int irqbit = ppc->irq_bit_offset + n;

    s->secppcintstat = deposit32(s->secppcintstat, irqbit, 1, level);
}

static void iotkit_secctl_init_ppc(IoTKitSecCtl *s,
                                   IoTKitSecCtlPPC *ppc,
                                   const char *name,
                                   int numports,
                                   int irq_bit_offset)
{
    char *gpioname;
    DeviceState *dev = DEVICE(s);

    ppc->numports = numports;
    ppc->irq_bit_offset = irq_bit_offset;
    ppc->parent = s;

    gpioname = g_strdup_printf("%s_nonsec", name);
    qdev_init_gpio_out_named(dev, ppc->nonsec, gpioname, numports);
    g_free(gpioname);
    gpioname = g_strdup_printf("%s_ap", name);
    qdev_init_gpio_out_named(dev, ppc->ap, gpioname, numports);
    g_free(gpioname);
    gpioname = g_strdup_printf("%s_irq_enable", name);
    qdev_init_gpio_out_named(dev, &ppc->irq_enable, gpioname, 1);
    g_free(gpioname);
    gpioname = g_strdup_printf("%s_irq_clear", name);
    qdev_init_gpio_out_named(dev, &ppc->irq_clear, gpioname, 1);
    g_free(gpioname);
    gpioname = g_strdup_printf("%s_irq_status", name);
    qdev_init_gpio_in_named_with_opaque(dev, iotkit_secctl_ppc_irqstatus,
                                        ppc, gpioname, 1);
    g_free(gpioname);
}

static void iotkit_secctl_init(Object *obj)
{
    IoTKitSecCtl *s = IOTKIT_SECCTL(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    DeviceState *dev = DEVICE(obj);
    int i;

    iotkit_secctl_init_ppc(s, &s->apb[0], "apb_ppc0",
                           IOTS_APB_PPC0_NUM_PORTS, 0);
    iotkit_secctl_init_ppc(s, &s->apb[1], "apb_ppc1",
                           IOTS_APB_PPC1_NUM_PORTS, 1);

    for (i = 0; i < IOTS_NUM_APB_EXP_PPC; i++) {
        IoTKitSecCtlPPC *ppc = &s->apbexp[i];
        char *ppcname = g_strdup_printf("apb_ppcexp%d", i);
        iotkit_secctl_init_ppc(s, ppc, ppcname, IOTS_PPC_NUM_PORTS, 4 + i);
        g_free(ppcname);
    }
    for (i = 0; i < IOTS_NUM_AHB_EXP_PPC; i++) {
        IoTKitSecCtlPPC *ppc = &s->ahbexp[i];
        char *ppcname = g_strdup_printf("ahb_ppcexp%d", i);
        iotkit_secctl_init_ppc(s, ppc, ppcname, IOTS_PPC_NUM_PORTS, 20 + i);
        g_free(ppcname);
    }

    qdev_init_gpio_out_named(dev, &s->sec_resp_cfg, "sec_resp_cfg", 1);
    qdev_init_gpio_out_named(dev, &s->nsc_cfg_irq, "nsc_cfg", 1);

    qdev_init_gpio_in_named(dev, iotkit_secctl_mpc_status, "mpc_status",
                            IOTS_NUM_MPC);
    qdev_init_gpio_in_named(dev, iotkit_secctl_mpcexp_status,
                            "mpcexp_status", IOTS_NUM_EXP_MPC);

    qdev_init_gpio_in_named(dev, iotkit_secctl_mscexp_status,
                            "mscexp_status", IOTS_NUM_EXP_MSC);
    qdev_init_gpio_out_named(dev, s->mscexp_clear, "mscexp_clear",
                             IOTS_NUM_EXP_MSC);
    qdev_init_gpio_out_named(dev, s->mscexp_ns, "mscexp_ns",
                             IOTS_NUM_EXP_MSC);
    qdev_init_gpio_out_named(dev, &s->msc_irq, "msc_irq", 1);

    memory_region_init_io(&s->s_regs, obj, &iotkit_secctl_s_ops,
                          s, "iotkit-secctl-s-regs", 0x1000);
    memory_region_init_io(&s->ns_regs, obj, &iotkit_secctl_ns_ops,
                          s, "iotkit-secctl-ns-regs", 0x1000);
    sysbus_init_mmio(sbd, &s->s_regs);
    sysbus_init_mmio(sbd, &s->ns_regs);
}

static const VMStateDescription iotkit_secctl_ppc_vmstate = {
    .name = "iotkit-secctl-ppc",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(ns, IoTKitSecCtlPPC),
        VMSTATE_UINT32(sp, IoTKitSecCtlPPC),
        VMSTATE_UINT32(nsp, IoTKitSecCtlPPC),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription iotkit_secctl_mpcintstatus_vmstate = {
    .name = "iotkit-secctl-mpcintstatus",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(mpcintstatus, IoTKitSecCtl),
        VMSTATE_END_OF_LIST()
    }
};

static bool needed_always(void *opaque)
{
    return true;
}

static const VMStateDescription iotkit_secctl_msc_vmstate = {
    .name = "iotkit-secctl/msc",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = needed_always,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(secmscintstat, IoTKitSecCtl),
        VMSTATE_UINT32(secmscinten, IoTKitSecCtl),
        VMSTATE_UINT32(nsmscexp, IoTKitSecCtl),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription iotkit_secctl_vmstate = {
    .name = "iotkit-secctl",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(secppcintstat, IoTKitSecCtl),
        VMSTATE_UINT32(secppcinten, IoTKitSecCtl),
        VMSTATE_UINT32(secrespcfg, IoTKitSecCtl),
        VMSTATE_UINT32(nsccfg, IoTKitSecCtl),
        VMSTATE_UINT32(brginten, IoTKitSecCtl),
        VMSTATE_STRUCT_ARRAY(apb, IoTKitSecCtl, IOTS_NUM_APB_PPC, 1,
                             iotkit_secctl_ppc_vmstate, IoTKitSecCtlPPC),
        VMSTATE_STRUCT_ARRAY(apbexp, IoTKitSecCtl, IOTS_NUM_APB_EXP_PPC, 1,
                             iotkit_secctl_ppc_vmstate, IoTKitSecCtlPPC),
        VMSTATE_STRUCT_ARRAY(ahbexp, IoTKitSecCtl, IOTS_NUM_AHB_EXP_PPC, 1,
                             iotkit_secctl_ppc_vmstate, IoTKitSecCtlPPC),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription*[]) {
        &iotkit_secctl_mpcintstatus_vmstate,
        &iotkit_secctl_msc_vmstate,
        NULL
    },
};

static void iotkit_secctl_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &iotkit_secctl_vmstate;
    dc->reset = iotkit_secctl_reset;
}

static const TypeInfo iotkit_secctl_info = {
    .name = TYPE_IOTKIT_SECCTL,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IoTKitSecCtl),
    .instance_init = iotkit_secctl_init,
    .class_init = iotkit_secctl_class_init,
};

static void iotkit_secctl_register_types(void)
{
    type_register_static(&iotkit_secctl_info);
}

type_init(iotkit_secctl_register_types);
