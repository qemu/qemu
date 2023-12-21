/*
 * ARM TrustZone master security controller emulation
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
#include "migration/vmstate.h"
#include "hw/registerfields.h"
#include "hw/irq.h"
#include "hw/misc/tz-msc.h"
#include "hw/qdev-properties.h"

static void tz_msc_update_irq(TZMSC *s)
{
    bool level = s->irq_status;

    trace_tz_msc_update_irq(level);
    qemu_set_irq(s->irq, level);
}

static void tz_msc_cfg_nonsec(void *opaque, int n, int level)
{
    TZMSC *s = TZ_MSC(opaque);

    trace_tz_msc_cfg_nonsec(level);
    s->cfg_nonsec = level;
}

static void tz_msc_cfg_sec_resp(void *opaque, int n, int level)
{
    TZMSC *s = TZ_MSC(opaque);

    trace_tz_msc_cfg_sec_resp(level);
    s->cfg_sec_resp = level;
}

static void tz_msc_irq_clear(void *opaque, int n, int level)
{
    TZMSC *s = TZ_MSC(opaque);

    trace_tz_msc_irq_clear(level);

    s->irq_clear = level;
    if (level) {
        s->irq_status = false;
        tz_msc_update_irq(s);
    }
}

/* The MSC may either block a transaction by aborting it, block a
 * transaction by making it RAZ/WI, allow it through with
 * MemTxAttrs indicating a secure transaction, or allow it with
 * MemTxAttrs indicating a non-secure transaction.
 */
typedef enum MSCAction {
    MSCBlockAbort,
    MSCBlockRAZWI,
    MSCAllowSecure,
    MSCAllowNonSecure,
} MSCAction;

static MSCAction tz_msc_check(TZMSC *s, hwaddr addr)
{
    /*
     * Check whether to allow an access from the bus master, returning
     * an MSCAction indicating the required behaviour. If the transaction
     * is blocked, the caller must check cfg_sec_resp to determine
     * whether to abort or RAZ/WI the transaction.
     */
    IDAUInterfaceClass *iic = IDAU_INTERFACE_GET_CLASS(s->idau);
    IDAUInterface *ii = IDAU_INTERFACE(s->idau);
    bool idau_exempt = false, idau_ns = true, idau_nsc = true;
    int idau_region = IREGION_NOTVALID;

    iic->check(ii, addr, &idau_region, &idau_exempt, &idau_ns, &idau_nsc);

    if (idau_exempt) {
        /*
         * Uncheck region -- OK, transaction type depends on
         * whether bus master is configured as Secure or NonSecure
         */
        return s->cfg_nonsec ? MSCAllowNonSecure : MSCAllowSecure;
    }

    if (idau_ns) {
        /* NonSecure region -- always forward as NS transaction */
        return MSCAllowNonSecure;
    }

    if (!s->cfg_nonsec) {
        /* Access to Secure region by Secure bus master: OK */
        return MSCAllowSecure;
    }

    /* Attempted access to Secure region by NS bus master: block */
    trace_tz_msc_access_blocked(addr);
    if (!s->cfg_sec_resp) {
        return MSCBlockRAZWI;
    }

    /*
     * The TRM isn't clear on behaviour if irq_clear is high when a
     * transaction is blocked. We assume that the MSC behaves like the
     * PPC, where holding irq_clear high suppresses the interrupt.
     */
    if (!s->irq_clear) {
        s->irq_status = true;
        tz_msc_update_irq(s);
    }
    return MSCBlockAbort;
}

static MemTxResult tz_msc_read(void *opaque, hwaddr addr, uint64_t *pdata,
                               unsigned size, MemTxAttrs attrs)
{
    TZMSC *s = opaque;
    AddressSpace *as = &s->downstream_as;
    uint64_t data;
    MemTxResult res;

    switch (tz_msc_check(s, addr)) {
    case MSCBlockAbort:
        return MEMTX_ERROR;
    case MSCBlockRAZWI:
        *pdata = 0;
        return MEMTX_OK;
    case MSCAllowSecure:
        attrs.secure = 1;
        attrs.unspecified = 0;
        break;
    case MSCAllowNonSecure:
        attrs.secure = 0;
        attrs.unspecified = 0;
        break;
    }

    switch (size) {
    case 1:
        data = address_space_ldub(as, addr, attrs, &res);
        break;
    case 2:
        data = address_space_lduw_le(as, addr, attrs, &res);
        break;
    case 4:
        data = address_space_ldl_le(as, addr, attrs, &res);
        break;
    case 8:
        data = address_space_ldq_le(as, addr, attrs, &res);
        break;
    default:
        g_assert_not_reached();
    }
    *pdata = data;
    return res;
}

static MemTxResult tz_msc_write(void *opaque, hwaddr addr, uint64_t val,
                                unsigned size, MemTxAttrs attrs)
{
    TZMSC *s = opaque;
    AddressSpace *as = &s->downstream_as;
    MemTxResult res;

    switch (tz_msc_check(s, addr)) {
    case MSCBlockAbort:
        return MEMTX_ERROR;
    case MSCBlockRAZWI:
        return MEMTX_OK;
    case MSCAllowSecure:
        attrs.secure = 1;
        attrs.unspecified = 0;
        break;
    case MSCAllowNonSecure:
        attrs.secure = 0;
        attrs.unspecified = 0;
        break;
    }

    switch (size) {
    case 1:
        address_space_stb(as, addr, val, attrs, &res);
        break;
    case 2:
        address_space_stw_le(as, addr, val, attrs, &res);
        break;
    case 4:
        address_space_stl_le(as, addr, val, attrs, &res);
        break;
    case 8:
        address_space_stq_le(as, addr, val, attrs, &res);
        break;
    default:
        g_assert_not_reached();
    }
    return res;
}

static const MemoryRegionOps tz_msc_ops = {
    .read_with_attrs = tz_msc_read,
    .write_with_attrs = tz_msc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void tz_msc_reset(DeviceState *dev)
{
    TZMSC *s = TZ_MSC(dev);

    trace_tz_msc_reset();
    s->cfg_sec_resp = false;
    s->cfg_nonsec = false;
    s->irq_clear = 0;
    s->irq_status = 0;
}

static void tz_msc_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    TZMSC *s = TZ_MSC(obj);

    qdev_init_gpio_in_named(dev, tz_msc_cfg_nonsec, "cfg_nonsec", 1);
    qdev_init_gpio_in_named(dev, tz_msc_cfg_sec_resp, "cfg_sec_resp", 1);
    qdev_init_gpio_in_named(dev, tz_msc_irq_clear, "irq_clear", 1);
    qdev_init_gpio_out_named(dev, &s->irq, "irq", 1);
}

static void tz_msc_realize(DeviceState *dev, Error **errp)
{
    Object *obj = OBJECT(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    TZMSC *s = TZ_MSC(dev);
    const char *name = "tz-msc-downstream";
    uint64_t size;

    /*
     * We can't create the upstream end of the port until realize,
     * as we don't know the size of the MR used as the downstream until then.
     * We insist on having a downstream, to avoid complicating the
     * code with handling the "don't know how big this is" case. It's easy
     * enough for the user to create an unimplemented_device as downstream
     * if they have nothing else to plug into this.
     */
    if (!s->downstream) {
        error_setg(errp, "MSC 'downstream' link not set");
        return;
    }
    if (!s->idau) {
        error_setg(errp, "MSC 'idau' link not set");
        return;
    }

    size = memory_region_size(s->downstream);
    address_space_init(&s->downstream_as, s->downstream, name);
    memory_region_init_io(&s->upstream, obj, &tz_msc_ops, s, name, size);
    sysbus_init_mmio(sbd, &s->upstream);
}

static const VMStateDescription tz_msc_vmstate = {
    .name = "tz-msc",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_BOOL(cfg_nonsec, TZMSC),
        VMSTATE_BOOL(cfg_sec_resp, TZMSC),
        VMSTATE_BOOL(irq_clear, TZMSC),
        VMSTATE_BOOL(irq_status, TZMSC),
        VMSTATE_END_OF_LIST()
    }
};

static Property tz_msc_properties[] = {
    DEFINE_PROP_LINK("downstream", TZMSC, downstream,
                     TYPE_MEMORY_REGION, MemoryRegion *),
    DEFINE_PROP_LINK("idau", TZMSC, idau,
                     TYPE_IDAU_INTERFACE, IDAUInterface *),
    DEFINE_PROP_END_OF_LIST(),
};

static void tz_msc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = tz_msc_realize;
    dc->vmsd = &tz_msc_vmstate;
    dc->reset = tz_msc_reset;
    device_class_set_props(dc, tz_msc_properties);
}

static const TypeInfo tz_msc_info = {
    .name = TYPE_TZ_MSC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(TZMSC),
    .instance_init = tz_msc_init,
    .class_init = tz_msc_class_init,
};

static void tz_msc_register_types(void)
{
    type_register_static(&tz_msc_info);
}

type_init(tz_msc_register_types);
