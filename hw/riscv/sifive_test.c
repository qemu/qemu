/*
 * QEMU SiFive Test Finisher
 *
 * Copyright (c) 2018 SiFive, Inc.
 *
 * Test finisher memory mapped device used to exit simulation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "sysemu/runstate.h"
#include "hw/hw.h"
#include "hw/riscv/sifive_test.h"

static uint64_t sifive_test_read(void *opaque, hwaddr addr, unsigned int size)
{
    return 0;
}

static void sifive_test_write(void *opaque, hwaddr addr,
           uint64_t val64, unsigned int size)
{
    if (addr == 0) {
        int status = val64 & 0xffff;
        int code = (val64 >> 16) & 0xffff;
        switch (status) {
        case FINISHER_FAIL:
            exit(code);
        case FINISHER_PASS:
            exit(0);
        case FINISHER_RESET:
            qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
            return;
        default:
            break;
        }
    }
    qemu_log_mask(LOG_GUEST_ERROR, "%s: write: addr=0x%x val=0x%016" PRIx64 "\n",
                  __func__, (int)addr, val64);
}

static const MemoryRegionOps sifive_test_ops = {
    .read = sifive_test_read,
    .write = sifive_test_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static void sifive_test_init(Object *obj)
{
    SiFiveTestState *s = SIFIVE_TEST(obj);

    memory_region_init_io(&s->mmio, obj, &sifive_test_ops, s,
                          TYPE_SIFIVE_TEST, 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static const TypeInfo sifive_test_info = {
    .name          = TYPE_SIFIVE_TEST,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SiFiveTestState),
    .instance_init = sifive_test_init,
};

static void sifive_test_register_types(void)
{
    type_register_static(&sifive_test_info);
}

type_init(sifive_test_register_types)


/*
 * Create Test device.
 */
DeviceState *sifive_test_create(hwaddr addr)
{
    DeviceState *dev = qdev_create(NULL, TYPE_SIFIVE_TEST);
    qdev_init_nofail(dev);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, addr);
    return dev;
}
