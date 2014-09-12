/*
 * Olimex Olimexino STM32 Maple Development Board
 *
 * Derived from hw/arm/stm32_p103.c
 *
 * Copyright (C) 2014 Marius Vlad
 *
 * Implementation based on
 * Olimex "OLIMEXINO-STM32 development board User's manual 2012"
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

#define LOG(format, ...)    do { \
        fprintf(stderr, format, ##__VA_ARGS__); \
} while (0)




typedef struct {
        Stm32 *stm32;
        bool last_button_pressed;
        qemu_irq button_irq;
} Stm32Maple;


static void led_irq_handler(void *opaque, int n, int level)
{
        /* There should only be one IRQ for the LED */
        assert(n == 0);

        /* Assume that the IRQ is only triggered if the LED has changed state.
         * If this is not correct, we may get multiple LED Offs or Ons in a row.
         */
        switch (level) {
                case 0:
                        printf("LED Off\n");
                        break;
                case 1:
                        printf("LED On\n");
                        break;
        }
}

static void led_err_irq_handler(void *opaque, int n, int level)
{
        /* There should only be one IRQ for the LED */
        assert(n == 0);

        /* Assume that the IRQ is only triggered if the LED has changed state.
         * If this is not correct, we may get multiple LED Offs or Ons in a row.
         */
        switch (level) {
                case 0:
                        printf("ERR LED Off\n");
                        break;
                case 1:
                        printf("ERR LED On\n");
                        break;
        }
}

static void stm32_maple_key_event(void *opaque, int keycode)
{
        Stm32Maple *s = (Stm32Maple *) opaque;
        bool make;
        int core_keycode;

        if ((keycode & 0x80) == 0) {
                make = true;
                core_keycode = keycode;
        } else {
                make = false;
                core_keycode = keycode & 0x7f;
        }

        /* Responds when a "B" key press is received.
         * Inside the monitor, you can type "sendkey b"
         */
        if (core_keycode == 0x30) {
                if (make) {
                        if (!s->last_button_pressed) {
                                qemu_irq_raise(s->button_irq);
                                s->last_button_pressed = true;
                        }
                } else {
                        if (s->last_button_pressed) {
                                qemu_irq_lower(s->button_irq);
                                s->last_button_pressed = false;
                        }
                }
        }
        return;

}


static void stm32_maple_init(MachineState *machine)
{
        const char *kernel_filename = machine->kernel_filename;
        qemu_irq *led_irq, *led_err_irq;
        Stm32Maple *s;

        s = (Stm32Maple *) g_malloc0(sizeof(Stm32Maple));

        /* flash, then ram */
        stm32_init(0x0001ffff, 0x00004fff, kernel_filename, 8000000, 32768);


        DeviceState *gpio_a = DEVICE(object_resolve_path("/machine/stm32/gpio[a]", NULL));
        DeviceState *gpio_c = DEVICE(object_resolve_path("/machine/stm32/gpio[c]", NULL));

        DeviceState *uart1 = DEVICE(object_resolve_path("/machine/stm32/uart[1]", NULL));
        DeviceState *uart2 = DEVICE(object_resolve_path("/machine/stm32/uart[2]", NULL));

        assert(gpio_a);
        assert(gpio_c);

        assert(uart1);
        assert(uart2);

        /* Connect LED to GPIO A pin 5 */
        led_irq = qemu_allocate_irqs(led_irq_handler, NULL, 1);
        qdev_connect_gpio_out(gpio_a, 5, led_irq[0]);

        /* Connect ERR LED to GPIO A pin 1 */
        led_err_irq = qemu_allocate_irqs(led_err_irq_handler, NULL, 1);
        qdev_connect_gpio_out(gpio_a, 1, led_err_irq[0]);

        /* Connect button to GPIO C pin 9 */
        s->button_irq = qdev_get_gpio_in(gpio_c, 9);
        qemu_add_kbd_event_handler(stm32_maple_key_event, s);

        /* Connect RS232 to UART */
        stm32_uart_connect((Stm32Uart *) uart1, 
                        serial_hds[0], STM32_USART1_NO_REMAP);

        /* useful for debugging */
        stm32_uart_connect((Stm32Uart *) uart2, 
                        serial_hds[1], STM32_USART2_NO_REMAP);
}

static QEMUMachine stm32_maple_machine = {
        .name = "stm32-maple",
        .desc = "OPEN SOURCE HARDWARE MAPLE / ARDUINO LIKE DEVELOPMENT BOARD",
        .init = stm32_maple_init,
};


static void stm32_maple_machine_init(void)
{
        qemu_register_machine(&stm32_maple_machine);
}

machine_init(stm32_maple_machine_init);
