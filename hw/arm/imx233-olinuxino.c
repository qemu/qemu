/*
 * imx233-olinuxino.c
 *
 * Copyright: Michel Pollet <buserror@gmail.com>
 *
 * QEMU Licence
 *
 * Support for a iMX233 development board. You can find reference for the
 * olinuxino boards on Olimex's website at:
 * https://www.olimex.com/Products/OLinuXino/iMX233/
 *
 * A typical instance of qemu can be created with the following command line:
    ./arm-softmmu/qemu-system-arm  -M imx233o -m 64M \
        -serial stdio -display none \
        -kernel /opt/minifs/build-imx233/vmlinuz-bare.dtb \
        -monitor telnet::4444,server,nowait -s \
        -sd /dev/loop0
    The kernel command line can also be specified with -append. However the default
    one should get a 3.x kernel booting with a working console.
 */

#include "hw/boards.h"
#include "hw/arm/mxs.h"
#include "hw/arm/arm.h"
#include "hw/sysbus.h"
#include "hw/i2c/bitbang_i2c.h"


static struct arm_boot_info imx233o_binfo = {
        /*
         * theorically, the load address 0 is for the 'bootlets'
         * however we don't support the bootlets yet, and the
         * kernel is happy decompressing itself from 0x0 as well
         * so it's not a big problem to start it from there.
         */
    .loader_start = 0x0,
    .board_id = 4005,   /* from linux's mach-types */
    .is_linux = 1,
};

enum {
    GPIO_SOFT_I2C_SDA = (0 * 32) + 25,  // GPMI_RDN
    GPIO_SOFT_I2C_SCL = (0 * 32) + 23,  // GPMI_WPN

    GPIO_W1 = (1 * 32) + 21,
    GPIO_HEATER = 51,
};

typedef struct {
    float temp;
    int on;
    qemu_irq set_temp;
    qemu_irq * in;
    QEMUTimer * timer;
} GPIOHeater;

static void gpio_heater_set(void *opaque, int irq, int level)
{
    GPIOHeater *h = opaque;
    h->on = !level;
    printf("QEMU %s %s\n", __func__, h->on ? "ON" : "OFF");
}

static void gpio_heater_timer(void *opaque)
{
    GPIOHeater *h = opaque;
    if (h->on)
        h->temp *= 1.001;
    else
        h->temp *= 0.9995;
    if (h->temp < 10.0)
    	h->temp = 10.0;
    else if (h->temp > 40)
    	h->temp = 40;
//    printf("QEMU %s %s %.2f\n", __func__, h->on ? "ON" : "OFF", h->temp);
    qemu_set_irq(h->set_temp, (int)(h->temp * 1000.0f));
    timer_mod(h->timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 1000);
}

static int
gpio_heater_init(GPIOHeater *h, qemu_irq set_temp)
{
    h->set_temp = set_temp;
    h->temp = 13.0f;
    h->on = 0;
    h->in = qemu_allocate_irqs(gpio_heater_set, h, 1);
    h->timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, gpio_heater_timer, h);
    timer_mod(h->timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 1000);
    return 0;
}

GPIOHeater heater;

ARMCPU * imx233_init(struct arm_boot_info * board_info);

static void imx233o_init(QEMUMachineInitArgs *args)
{
    struct arm_boot_info * board_info = &imx233o_binfo;
    ARMCPU *cpu = NULL;

    board_info->ram_size = ram_size;
    board_info->kernel_filename = args->kernel_filename;
    board_info->kernel_cmdline =
            args->kernel_cmdline ?
                    args->kernel_cmdline :
                    "console=ttyAMA0,115200 ro root=/dev/mmcblk0p2 ssp1=mmc loglevel=7";
    board_info->nb_cpus = 1;

    cpu = imx233_init(board_info);

    /*
     * Recover the pin controller of the imx23.
     * NOTE: that the device has to explicitly set it's 'name' for
     * qdev_find_recursive() to work
     */
    DeviceState * gpio = qdev_find_recursive(sysbus_get_default(), "imx23_pinctrl");
    /*
     * Hook up a gpio-i2c bus to the pins that are reserved for that in
     * the olinuxino .dts file, and add a RTC and an eeprom on it, because
     * we can.
     */
    {
        DeviceState * dev = sysbus_create_simple("gpio_i2c", -1, 0);

        qdev_connect_gpio_out(gpio, GPIO_SOFT_I2C_SDA,
                qdev_get_gpio_in(dev, BITBANG_I2C_SDA));
        qdev_connect_gpio_out(dev, BITBANG_I2C_SDA,
                qdev_get_gpio_in(gpio, GPIO_SOFT_I2C_SDA));

        qdev_connect_gpio_out(gpio, GPIO_SOFT_I2C_SCL,
                qdev_get_gpio_in(dev, BITBANG_I2C_SCL));

        i2c_bus *bus = (i2c_bus *)qdev_get_child_bus(dev, "i2c");
        printf("bus = %p\n", bus);
        i2c_create_slave(bus, "ds1338", 0x68);
    }
    /*
     * Add a onewire DS18S20 thermal sensor too. Theres one bidirectional GPIO
     */
    {
        DeviceState * dev = sysbus_create_simple("ds18s20", -1, 0);

        qdev_connect_gpio_out(gpio, GPIO_W1, qdev_get_gpio_in(dev, 0));
        qdev_connect_gpio_out(dev, 0, qdev_get_gpio_in(gpio, GPIO_W1));

        /*
         * Hookup a virtual GPIO to a relay that change the
         */
        {
            gpio_heater_init(&heater, qdev_get_gpio_in(dev, 1));
            qdev_connect_gpio_out(gpio, GPIO_HEATER, heater.in[0]);
        }
    }
    arm_load_kernel(cpu, board_info);

}

static QEMUMachine imx233o_machine = {
    .name = "imx233o",
    .desc = "i.MX233 Olinuxino (ARM926)",
    .init = imx233o_init,
};

static void imx233o_machine_init(void)
{
    qemu_register_machine(&imx233o_machine);
}

machine_init(imx233o_machine_init)
