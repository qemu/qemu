/*
 * Dropwatcher flashboard
 *
 * Copyright (C) 2010 Andre Beckus 2014 Andrew Hankins
 *
 * Implementation based on
 * Shit Jony told me
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "hw/arm/stm32.h"
#include "hw/sysbus.h"
#include "hw/arm/arm.h"
#include "hw/devices.h"
#include "ui/console.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"

#if 0
typedef struct 
{
    I2CSlave i2c;
    int len;
    uint8_t buf[3];

} MCP4017State;

static int mcp4017_send(I2CSlave *i2c, uint8_t data)
{
    MCP4017State *s = FROM_I2C_SLAVE(MCP4017State, i2c);
    s->buf[s->len] = data;
    if (s->len++ > 2) {
        DPRINTF("%s: message too long (%i bytes)\n",
            __func__, s->len);
        return 1;
    }

    if (s->len == 2) {
        DPRINTF("%s: reg %d value 0x%02x\n", __func__,
                s->buf[0], s->buf[1]);
    }

    return 0;
}

static void mcp4017_event(I2CSlave *i2c, enum i2c_event event)
{
    MCP4017State *s = FROM_I2C_SLAVE(MCP4017State, i2c);
    switch (event) {
    case I2C_START_SEND:
        s->len = 0;
        break;
    case I2C_START_RECV:
        if (s->len != 1) {
            DPRINTF("%s: short message!?\n", __func__);
        }
        break;
    case I2C_FINISH:
        break;
    default:
        break;
    }
}

static int mcp4017_recv(I2CSlave *slave)
{
    int retval = 0x00;
    MCP4017State *s = FROM_I2C_SLAVE(MCP4017State, slave);

    switch (s->buf[0]) {
    /* Return hardcoded battery voltage,
     * 0xf0 means ~4.1V
     */
    case 0x02:
        retval = 0xf0;
        break;
    /* Return 0x00 for other regs,
     * we don't know what they are for,
     * anyway they return 0x00 on real hardware.
     */
    default:
        break;
    }

    return retval;
}

static int mcp4017_init(I2CSlave *i2c)
{
    /* Nothing to do.  */
    return 0;
}

static VMStateDescription vmstate_mcp4017_state = {
    .name = "mcp4017",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_INT32(len, MCP4017State),
        VMSTATE_BUFFER(buf, MCP4017State),
        VMSTATE_END_OF_LIST(),
    }
};
static void mcp4017_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    k->init  = mcp4017_init;
    k->event = mcp4017_event;
    k->recv  = mcp4017_recv;
    k->send  = mcp4017_send;
    dc->vmsd = &vmstate_mcp4017_state;
}

static const TypeInfo mcp4017_info = {
    .name          = "mcp4017",
    .parent        = TYPE_I2C_SLAVE,
    .instance_size = sizeof(MCP4017State),
    .class_init    = mcp4017_class_init,
};
#endif

typedef struct
{
    Stm32 *stm32;

    bool     triggered;
    qemu_irq gpioIRQ;
    qemu_irq odIRQ;

    bool ledDrive1;
    bool ledDrive2;

} Stm32Flashboard;

static void printLedStatus(Stm32Flashboard *s)
{
    int64_t now = qemu_get_clock_ns(vm_clock);
    int64_t secs = now / 1000000000;
    int64_t frac = (now % 1000000000);

    if (!s->ledDrive1 && !s->ledDrive2)
    {
        printf("(%"PRId64".%09"PRId64") LED %s\n", secs, frac, "Shutdown");
    }
    else if (s->ledDrive1 && !s->ledDrive2)
    {
        printf("(%"PRId64".%09"PRId64") LED %s\n", secs, frac, "Low current");
    }
    else if (!s->ledDrive1 && s->ledDrive2)
    {
        printf("(%"PRId64".%09"PRId64") LED %s\n", secs, frac, "High current");
    }
    else if (s->ledDrive1 && s->ledDrive2)
    {
        printf("(%"PRId64".%09"PRId64") LED %s\n", secs, frac, "Low+High current");
    }
}

static void ledDrive1_irq_handler(void *opaque, int n, int level)
{
    /* There should only be one IRQ for the LED */
    assert(n == 0);

    Stm32Flashboard *s = (Stm32Flashboard*) opaque;

    /* Assume that the IRQ is only triggered if the LED has changed state.
     * If this is not correct, we may get multiple LED Offs or Ons in a row.
     */
    switch (level) {
        case 0:
            s->ledDrive1 = false;
            break;
        case 1:
            s->ledDrive1 = true;
            break;
    }
    printLedStatus(s);
}

static void ledDrive2_irq_handler(void *opaque, int n, int level)
{
    /* There should only be one IRQ for the LED */
    assert(n == 0);

    Stm32Flashboard *s = (Stm32Flashboard*) opaque;

    /* Assume that the IRQ is only triggered if the LED has changed state.
     * If this is not correct, we may get multiple LED Offs or Ons in a row.
     */
    switch (level) {
        case 0:
            s->ledDrive2 = false;
            break;
        case 1:
            s->ledDrive2 = true;
            break;
    }
    printLedStatus(s);
}

static void gpiob_irq_handler(void *opaque, int n, int level)
{
    int64_t now = qemu_get_clock_ns(vm_clock);
    int64_t secs = now / 1000000000;
    int64_t frac = (now % 1000000000);

    int gpio = (int) opaque;
    const char *name;
    switch (gpio)
    {
        case 1: name = "GPIO 1"; break;
        case 2: name = "GPIO 2"; break;
        case 3: name = "GPIO 3"; break;
        case 4: name = "GPIO 7"; break;
        case 5: name = "GPIO 6"; break;
        case 6: name = "GPIO 5"; break;
        case 7: name = "GPIO 4"; break;
        case 8: name = "GPIO EN"; break;
        case 9: name = "OD IN"; break;
        case 10: name = "OD 1"; break;
        case 11: name = "OD 2"; break;
    }

    /* Assume that the IRQ is only triggered if the LED has changed state.
     * If this is not correct, we may get multiple LED Offs or Ons in a row.
     */
    switch (level) {
        case 0:
            printf("(%"PRId64".%09"PRId64") %s Off\n", secs, frac, name);
            break;
        case 1:
            printf("(%"PRId64".%09"PRId64") %s On\n", secs, frac, name);
            break;
    }
}

static void stm32_flashboard_key_event(void *opaque, int keycode)
{
    Stm32Flashboard *s = (Stm32Flashboard *)opaque;
    bool make;
    int core_keycode;

    if((keycode & 0x80) == 0) {
        make = true;
        core_keycode = keycode;
    } else {
        make = false;
        core_keycode = keycode & 0x7f;
    }

    /* Responds when a "B" key press is received.
     * Inside the monitor, you can type "sendkey b"
     */
    if(core_keycode == 0x30) 
    {
        if(make) {
            if(!s->triggered) {
                int64_t now = qemu_get_clock_ns(vm_clock);
                int64_t secs = now / 1000000000;
                int64_t frac = (now % 1000000000);
                printf("(%"PRId64".%09"PRId64") Trigger GPIO 0\n", secs, frac);
                qemu_irq_raise(s->gpioIRQ);
                s->triggered = true;
            }
        } else {
            if(s->triggered) {
                qemu_irq_lower(s->gpioIRQ);
                s->triggered = false;
            }
        }
    }
    else if (core_keycode == 0x31)
    {
        if(make) {
            if(!s->triggered) {
                int64_t now = qemu_get_clock_ns(vm_clock);
                int64_t secs = now / 1000000000;
                int64_t frac = (now % 1000000000);
                printf("(%"PRId64".%09"PRId64") Trigger OD_IN\n", secs, frac);
                qemu_irq_raise(s->odIRQ);
                s->triggered = true;
            }
        } else {
            if(s->triggered) {
                qemu_irq_lower(s->odIRQ);
                s->triggered = false;
            }
        }
    }
    return;
}

#define OUTPUTGPIOS 10

static void stm32_flashboard_init(QEMUMachineInitArgs *args)
{
    int i;
    const char* kernel_filename = args->kernel_filename;
    qemu_irq *ledDriver1IRQ, *ledDriver2IRQ, *gpiob[OUTPUTGPIOS];
    Stm32Flashboard *s;

    s = (Stm32Flashboard *)g_malloc0(sizeof(Stm32Flashboard));

    stm32_init(/*flash_size*/0x00020000,
               /*ram_size*/0x00004fff,
               kernel_filename,
               12000000,
               32768);

    DeviceState *gpio_a = DEVICE(object_resolve_path("/machine/stm32/gpio[a]", NULL));
    DeviceState *gpio_b = DEVICE(object_resolve_path("/machine/stm32/gpio[b]", NULL));
    DeviceState *uart1 = DEVICE(object_resolve_path("/machine/stm32/uart[1]", NULL));
    DeviceState *uart2 = DEVICE(object_resolve_path("/machine/stm32/uart[2]", NULL));
    DeviceState *i2c1 = DEVICE(object_resolve_path("/machine/stm32/i2c[1]", NULL));

    assert(gpio_a);
    assert(gpio_b);
    assert(uart1);
    assert(uart2);
    //assert(i2c1);

    /* Connect LED_DRIVER_1 to GPIO A pin 1 */
    ledDriver1IRQ = qemu_allocate_irqs(ledDrive1_irq_handler, s, 1);
    qdev_connect_gpio_out(gpio_a, 1, ledDriver1IRQ[0]);

    /* Connect LED_DRIVER_2 to GPIO A pin 6 */
    ledDriver2IRQ = qemu_allocate_irqs(ledDrive2_irq_handler, s, 1);
    qdev_connect_gpio_out(gpio_a, 6, ledDriver2IRQ[0]);

    /* Connect trigger to GPIO B pin 8 - GPIO0 */
    s->gpioIRQ = qdev_get_gpio_in(gpio_b, 8);
    qemu_add_kbd_event_handler(stm32_flashboard_key_event, s);

    /* Connect GPIO B pin 9-15 - GPIO1-8 */
    for (i = 0; i < 7; i++)
    {
        gpiob[i] = qemu_allocate_irqs(gpiob_irq_handler, (void*)i+1, 1);
        qdev_connect_gpio_out(gpio_b, 9+i, gpiob[i][0]);
    }

    /* Connect the OD outputs */
    gpiob[i] = qemu_allocate_irqs(gpiob_irq_handler, (void*)10, 1);
    qdev_connect_gpio_out(gpio_b, 1, gpiob[i][0]);
    i++;
    gpiob[i] = qemu_allocate_irqs(gpiob_irq_handler, (void*)11, 1);
    qdev_connect_gpio_out(gpio_b, 2, gpiob[i][0]);
    i++;

    /* Connect the GPIO EN outputs */
    gpiob[i] = qemu_allocate_irqs(gpiob_irq_handler, (void*)8, 1);
    qdev_connect_gpio_out(gpio_b, 5, gpiob[i][0]);
    i++;

    /* Connect trigger to GPIO B pin 0 - OD_IN */
    s->odIRQ = qdev_get_gpio_in(gpio_b, 0);
    qemu_add_kbd_event_handler(stm32_flashboard_key_event, s);

    /* Connect RS232 to UART */
    stm32_uart_connect(
            (Stm32Uart *)uart1,
            serial_hds[0],
            STM32_USART1_NO_REMAP);

    stm32_uart_connect(
            (Stm32Uart *)uart2,
            serial_hds[1],
            STM32_USART2_NO_REMAP);
    
    /* Connect I2C */
    //type_register_static(&mcp4017_info);
 }

static QEMUMachine stm32_flashboard_machine = {
    .name = "stm32-flashboard",
    .desc = "Dropwatcher flashboard",
    .init = stm32_flashboard_init,
    DEFAULT_MACHINE_OPTIONS,
};

static void stm32_flashboard_machine_init(void)
{
    qemu_register_machine(&stm32_flashboard_machine);
}

machine_init(stm32_flashboard_machine_init);
