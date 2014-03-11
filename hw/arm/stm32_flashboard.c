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


typedef struct
{
    Stm32 *stm32;

    bool     triggered;
    qemu_irq triggerIRQ;

    bool ledDrive1;
    bool ledDrive2;

} Stm32Flashboard;

static void printLedStatus(Stm32Flashboard *s)
{
    int64_t now = qemu_get_clock_ns(vm_clock);

    if (!s->ledDrive1 && !s->ledDrive2)
    {
        printf("(%"PRId64") LED %s\n", now, "Shutdown");
    }
    else if (s->ledDrive1 && !s->ledDrive2)
    {
        printf("(%"PRId64") LED %s\n", now, "Low current");
    }
    else if (!s->ledDrive1 && s->ledDrive2)
    {
        printf("(%"PRId64") LED %s\n", now, "High current");
    }
    else if (s->ledDrive1 && s->ledDrive2)
    {
        printf("(%"PRId64") LED %s\n", now, "Low+High current");
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
    /* There should only be one IRQ for the LED */
    assert(n == 0);

    int gpio = ((int)opaque);

    int64_t now = qemu_get_clock_ns(vm_clock);

    /* Assume that the IRQ is only triggered if the LED has changed state.
     * If this is not correct, we may get multiple LED Offs or Ons in a row.
     */
    switch (level) {
        case 0:
            printf("(%ld) GPIO[%d] Off\n", now, gpio);
            break;
        case 1:
            printf("(%ld) GPIO[%d] On\n", now, gpio);
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
    if(core_keycode == 0x30) {
        if(make) {
            if(!s->triggered) {
                qemu_irq_raise(s->triggerIRQ);
                s->triggered = true;
            }
        } else {
            if(s->triggered) {
                qemu_irq_lower(s->triggerIRQ);
                s->triggered = false;
            }
        }
    }
    return;
}

#define OUTPUTGPIOS 7

static void stm32_flashboard_init(QEMUMachineInitArgs *args)
{
    int i;
    const char* kernel_filename = args->kernel_filename;
    qemu_irq *ledDriver1IRQ, *ledDriver2IRQ, *gpiob[OUTPUTGPIOS];
    Stm32Flashboard *s;

    s = (Stm32Flashboard *)g_malloc0(sizeof(Stm32Flashboard));

    stm32_init(/*flash_size*/0x00010000,
               /*ram_size*/0x00004fff,
               kernel_filename,
               12000000,
               32768);

    DeviceState *gpio_a = DEVICE(object_resolve_path("/machine/stm32/gpio[a]", NULL));
    DeviceState *gpio_b = DEVICE(object_resolve_path("/machine/stm32/gpio[b]", NULL));
    DeviceState *uart1 = DEVICE(object_resolve_path("/machine/stm32/uart[1]", NULL));
    DeviceState *uart2 = DEVICE(object_resolve_path("/machine/stm32/uart[2]", NULL));

    assert(gpio_a);
    assert(gpio_b);
    assert(uart1);
    assert(uart2);

    /* Connect LED_DRIVER_1 to GPIO A pin 1 */
    ledDriver1IRQ = qemu_allocate_irqs(ledDrive1_irq_handler, s, 1);
    qdev_connect_gpio_out(gpio_a, 1, ledDriver1IRQ[0]);

    /* Connect LED_DRIVER_2 to GPIO A pin 6 */
    ledDriver2IRQ = qemu_allocate_irqs(ledDrive2_irq_handler, s, 1);
    qdev_connect_gpio_out(gpio_a, 6, ledDriver2IRQ[0]);

    /* Connect trigger to GPIO B pin 8 - GPIO0 */
    s->triggerIRQ = qdev_get_gpio_in(gpio_b, 8);
    qemu_add_kbd_event_handler(stm32_flashboard_key_event, s);

    /* Connect GPIO B pin 9-15 - GPIO1-8 */
    for (i = 0; i < OUTPUTGPIOS; i++)
    {
        gpiob[i] = qemu_allocate_irqs(gpiob_irq_handler, i+1, 1);
        qdev_connect_gpio_out(gpio_b, 9+i, gpiob[i][0]);
    }

    /* Connect RS232 to UART */
    stm32_uart_connect(
            (Stm32Uart *)uart1,
            serial_hds[0],
            STM32_USART1_NO_REMAP);

    stm32_uart_connect(
            (Stm32Uart *)uart2,
            serial_hds[1],
            STM32_USART2_NO_REMAP);
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
