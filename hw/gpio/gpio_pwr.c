/*
 * GPIO qemu power controller
 *
 * Copyright (c) 2020 Linaro Limited
 *
 * Author: Maxim Uvarov <maxim.uvarov@linaro.org>
 *
 * Virtual gpio driver which can be used on top of pl061
 * to reboot and shutdown qemu virtual machine. One of use
 * case is gpio driver for secure world application (ARM
 * Trusted Firmware.).
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * QEMU interface:
 * two named input GPIO lines:
 *   'reset' : when asserted, trigger system reset
 *   'shutdown' : when asserted, trigger system shutdown
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "sysemu/runstate.h"

#define TYPE_GPIOPWR "gpio-pwr"
OBJECT_DECLARE_SIMPLE_TYPE(GPIO_PWR_State, GPIOPWR)

struct GPIO_PWR_State {
    SysBusDevice parent_obj;
};

static void gpio_pwr_reset(void *opaque, int n, int level)
{
    if (level) {
        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
    }
}

static void gpio_pwr_shutdown(void *opaque, int n, int level)
{
    if (level) {
        qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
    }
}

static void gpio_pwr_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);

    qdev_init_gpio_in_named(dev, gpio_pwr_reset, "reset", 1);
    qdev_init_gpio_in_named(dev, gpio_pwr_shutdown, "shutdown", 1);
}

static const TypeInfo gpio_pwr_info = {
    .name          = TYPE_GPIOPWR,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(GPIO_PWR_State),
    .instance_init = gpio_pwr_init,
};

static void gpio_pwr_register_types(void)
{
    type_register_static(&gpio_pwr_info);
}

type_init(gpio_pwr_register_types)
