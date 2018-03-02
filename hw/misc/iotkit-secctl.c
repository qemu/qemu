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

static MemTxResult iotkit_secctl_s_read(void *opaque, hwaddr addr,
                                        uint64_t *pdata,
                                        unsigned size, MemTxAttrs attrs)
{
    uint64_t r;
    uint32_t offset = addr & ~0x3;

    switch (offset) {
    case A_AHBNSPPC0:
    case A_AHBSPPPC0:
        r = 0;
        break;
    case A_SECRESPCFG:
    case A_NSCCFG:
    case A_SECMPCINTSTATUS:
    case A_SECPPCINTSTAT:
    case A_SECPPCINTEN:
    case A_SECMSCINTSTAT:
    case A_SECMSCINTEN:
    case A_BRGINTSTAT:
    case A_BRGINTEN:
    case A_AHBNSPPCEXP0:
    case A_AHBNSPPCEXP1:
    case A_AHBNSPPCEXP2:
    case A_AHBNSPPCEXP3:
    case A_APBNSPPC0:
    case A_APBNSPPC1:
    case A_APBNSPPCEXP0:
    case A_APBNSPPCEXP1:
    case A_APBNSPPCEXP2:
    case A_APBNSPPCEXP3:
    case A_AHBSPPPCEXP0:
    case A_AHBSPPPCEXP1:
    case A_AHBSPPPCEXP2:
    case A_AHBSPPPCEXP3:
    case A_APBSPPPC0:
    case A_APBSPPPC1:
    case A_APBSPPPCEXP0:
    case A_APBSPPPCEXP1:
    case A_APBSPPPCEXP2:
    case A_APBSPPPCEXP3:
    case A_NSMSCEXP:
        qemu_log_mask(LOG_UNIMP,
                      "IoTKit SecCtl S block read: "
                      "unimplemented offset 0x%x\n", offset);
        r = 0;
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

static MemTxResult iotkit_secctl_s_write(void *opaque, hwaddr addr,
                                         uint64_t value,
                                         unsigned size, MemTxAttrs attrs)
{
    uint32_t offset = addr;

    trace_iotkit_secctl_s_write(offset, value, size);

    if (size != 4) {
        /* Byte and halfword writes are ignored */
        qemu_log_mask(LOG_GUEST_ERROR,
                      "IotKit SecCtl S block write: bad size, ignored\n");
        return MEMTX_OK;
    }

    switch (offset) {
    case A_SECRESPCFG:
    case A_NSCCFG:
    case A_SECPPCINTCLR:
    case A_SECPPCINTEN:
    case A_SECMSCINTCLR:
    case A_SECMSCINTEN:
    case A_BRGINTCLR:
    case A_BRGINTEN:
    case A_AHBNSPPCEXP0:
    case A_AHBNSPPCEXP1:
    case A_AHBNSPPCEXP2:
    case A_AHBNSPPCEXP3:
    case A_APBNSPPC0:
    case A_APBNSPPC1:
    case A_APBNSPPCEXP0:
    case A_APBNSPPCEXP1:
    case A_APBNSPPCEXP2:
    case A_APBNSPPCEXP3:
    case A_AHBSPPPCEXP0:
    case A_AHBSPPPCEXP1:
    case A_AHBSPPPCEXP2:
    case A_AHBSPPPCEXP3:
    case A_APBSPPPC0:
    case A_APBSPPPC1:
    case A_APBSPPPCEXP0:
    case A_APBSPPPCEXP1:
    case A_APBSPPPCEXP2:
    case A_APBSPPPCEXP3:
        qemu_log_mask(LOG_UNIMP,
                      "IoTKit SecCtl S block write: "
                      "unimplemented offset 0x%x\n", offset);
        break;
    case A_SECMPCINTSTATUS:
    case A_SECPPCINTSTAT:
    case A_SECMSCINTSTAT:
    case A_BRGINTSTAT:
    case A_AHBNSPPC0:
    case A_AHBSPPPC0:
    case A_NSMSCEXP:
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
    case A_APBNSPPPC0:
    case A_APBNSPPPC1:
    case A_APBNSPPPCEXP0:
    case A_APBNSPPPCEXP1:
    case A_APBNSPPPCEXP2:
    case A_APBNSPPPCEXP3:
        qemu_log_mask(LOG_UNIMP,
                      "IoTKit SecCtl NS block read: "
                      "unimplemented offset 0x%x\n", offset);
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
    uint32_t offset = addr;

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
    case A_APBNSPPPC0:
    case A_APBNSPPPC1:
    case A_APBNSPPPCEXP0:
    case A_APBNSPPPCEXP1:
    case A_APBNSPPPCEXP2:
    case A_APBNSPPPCEXP3:
        qemu_log_mask(LOG_UNIMP,
                      "IoTKit SecCtl NS block write: "
                      "unimplemented offset 0x%x\n", offset);
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

static void iotkit_secctl_reset(DeviceState *dev)
{

}

static void iotkit_secctl_init(Object *obj)
{
    IoTKitSecCtl *s = IOTKIT_SECCTL(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->s_regs, obj, &iotkit_secctl_s_ops,
                          s, "iotkit-secctl-s-regs", 0x1000);
    memory_region_init_io(&s->ns_regs, obj, &iotkit_secctl_ns_ops,
                          s, "iotkit-secctl-ns-regs", 0x1000);
    sysbus_init_mmio(sbd, &s->s_regs);
    sysbus_init_mmio(sbd, &s->ns_regs);
}

static const VMStateDescription iotkit_secctl_vmstate = {
    .name = "iotkit-secctl",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST()
    }
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
