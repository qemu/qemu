/*
 * Microbit stub for Nordic Semiconductor nRF51 SoC Two-Wire Interface
 * http://infocenter.nordicsemi.com/pdf/nRF51_RM_v3.0.1.pdf
 *
 * This is a microbit-specific stub for the TWI controller on the nRF51 SoC.
 * We don't emulate I2C devices but the firmware probes the
 * accelerometer/magnetometer on startup and panics if they are not found.
 * Therefore we stub out the probing.
 *
 * In the future this file could evolve into a full nRF51 TWI controller
 * device.
 *
 * Copyright 2018 Steffen Görtz <contrib@steffen-goertz.de>
 * Copyright 2019 Red Hat, Inc.
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/i2c/microbit_i2c.h"
#include "migration/vmstate.h"

static const uint32_t twi_read_sequence[] = {0x5A, 0x5A, 0x40};

static uint64_t microbit_i2c_read(void *opaque, hwaddr addr, unsigned int size)
{
    MicrobitI2CState *s = opaque;
    uint64_t data = 0x00;

    switch (addr) {
    case NRF51_TWI_EVENT_STOPPED:
        data = 0x01;
        break;
    case NRF51_TWI_EVENT_RXDREADY:
        data = 0x01;
        break;
    case NRF51_TWI_EVENT_TXDSENT:
        data = 0x01;
        break;
    case NRF51_TWI_REG_RXD:
        data = twi_read_sequence[s->read_idx];
        if (s->read_idx < G_N_ELEMENTS(twi_read_sequence)) {
            s->read_idx++;
        }
        break;
    default:
        data = s->regs[addr / sizeof(s->regs[0])];
        break;
    }

    qemu_log_mask(LOG_UNIMP, "%s: 0x%" HWADDR_PRIx " [%u] = %" PRIx32 "\n",
                  __func__, addr, size, (uint32_t)data);


    return data;
}

static void microbit_i2c_write(void *opaque, hwaddr addr, uint64_t data,
                               unsigned int size)
{
    MicrobitI2CState *s = opaque;

    qemu_log_mask(LOG_UNIMP, "%s: 0x%" HWADDR_PRIx " <- 0x%" PRIx64 " [%u]\n",
                  __func__, addr, data, size);
    s->regs[addr / sizeof(s->regs[0])] = data;
}

static const MemoryRegionOps microbit_i2c_ops = {
    .read = microbit_i2c_read,
    .write = microbit_i2c_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

static const VMStateDescription microbit_i2c_vmstate = {
    .name = TYPE_MICROBIT_I2C,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, MicrobitI2CState, MICROBIT_I2C_NREGS),
        VMSTATE_UINT32(read_idx, MicrobitI2CState),
    },
};

static void microbit_i2c_reset(DeviceState *dev)
{
    MicrobitI2CState *s = MICROBIT_I2C(dev);

    memset(s->regs, 0, sizeof(s->regs));
    s->read_idx = 0;
}

static void microbit_i2c_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    MicrobitI2CState *s = MICROBIT_I2C(dev);

    memory_region_init_io(&s->iomem, OBJECT(s), &microbit_i2c_ops, s,
                          "microbit.twi", NRF51_TWI_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void microbit_i2c_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &microbit_i2c_vmstate;
    dc->reset = microbit_i2c_reset;
    dc->realize = microbit_i2c_realize;
    dc->desc = "Microbit I2C controller";
}

static const TypeInfo microbit_i2c_info = {
    .name = TYPE_MICROBIT_I2C,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MicrobitI2CState),
    .class_init = microbit_i2c_class_init,
};

static void microbit_i2c_register_types(void)
{
    type_register_static(&microbit_i2c_info);
}

type_init(microbit_i2c_register_types)
