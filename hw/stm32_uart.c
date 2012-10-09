/*
 * STM32 Microcontroller UART module
 *
 * Copyright (C) 2010 Andre Beckus
 *
 * Source code based on pl011.c
 * Implementation based on ST Microelectronics "RM0008 Reference Manual Rev 10"
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

#include "sysbus.h"
#include "stm32.h"



/* DEFINITIONS*/

/* See the README file for details on these settings. */
//#define DEBUG_STM32_UART
//#define STM32_UART_NO_BAUD_DELAY
//#define STM32_UART_ENABLE_OVERRUN

#ifdef DEBUG_STM32_UART
#define DPRINTF(fmt, ...)                                       \
    do { printf("STM32_UART: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...)
#endif

#define USART_SR_OFFSET 0x00
#define USART_SR_TXE_BIT 7
#define USART_SR_TC_BIT 6
#define USART_SR_RXNE_BIT 5
#define USART_SR_ORE_BIT 3

#define USART_DR_OFFSET 0x04

#define USART_BRR_OFFSET 0x08

#define USART_CR1_OFFSET 0x0c
#define USART_CR1_UE_BIT 13
#define USART_CR1_M_BIT 12
#define USART_CR1_PCE_BIT 10
#define USART_CR1_PS_BIT 9
#define USART_CR1_TXEIE_BIT 7
#define USART_CR1_TCIE_BIT 6
#define USART_CR1_RXNEIE_BIT 5
#define USART_CR1_TE_BIT 3
#define USART_CR1_RE_BIT 2

#define USART_CR2_OFFSET 0x10
#define USART_CR2_STOP_START 12
#define USART_CR2_STOP_MASK 0x00003000

#define USART_CR3_OFFSET 0x14
#define USART_CR3_CTSE_BIT 9
#define USART_CR3_RTSE_BIT 8

#define USART_GTPR_OFFSET 0x18


struct Stm32Uart {
    /* Inherited */
    SysBusDevice busdev;

    /* Properties */
    stm32_periph_t periph;
    void *stm32_rcc_prop;
    void *stm32_gpio_prop;
    void *stm32_afio_prop;

    /* Private */
    MemoryRegion iomem;

    Stm32Rcc *stm32_rcc;
    Stm32Gpio **stm32_gpio;
    Stm32Afio *stm32_afio;

    int uart_index;

    uint32_t bits_per_sec;
    int64_t ns_per_char;

    /* Register Values */
    uint32_t
        USART_RDR,
        USART_TDR,
        USART_BRR,
        USART_CR1,
        USART_CR2,
        USART_CR3;

    /* Register Field Values */
    uint32_t
        USART_SR_TXE,
        USART_SR_TC,
        USART_SR_RXNE,
        USART_SR_ORE,
        USART_CR1_UE,
        USART_CR1_TXEIE,
        USART_CR1_TCIE,
        USART_CR1_RXNEIE,
        USART_CR1_TE,
        USART_CR1_RE;

    bool sr_read_since_ore_set;

    /* Indicates whether the USART is currently receiving a byte. */
    bool receiving;

    /* Timers used to simulate a delay corresponding to the baud rate. */
    struct QEMUTimer *rx_timer;
    struct QEMUTimer *tx_timer;

    CharDriverState *chr;

    /* Stores the USART pin mapping used by the board.  This is used to check
     * the AFIO's USARTx_REMAP register to make sure the software has set
     * the correct mapping.
     */
    uint32_t afio_board_map;

    qemu_irq irq;
    int curr_irq_level;
};






/* HELPER FUNCTIONS */

/* Update the baud rate based on the USART's peripheral clock frequency. */
static void stm32_uart_baud_update(Stm32Uart *s)
{
    uint32_t clk_freq = stm32_rcc_get_periph_freq(s->stm32_rcc, s->periph);
    uint64_t ns_per_bit;

    if((s->USART_BRR == 0) || (clk_freq == 0)) {
        s->bits_per_sec = 0;
    } else {
        s->bits_per_sec = clk_freq / s->USART_BRR;
        ns_per_bit = 1000000000LL / s->bits_per_sec;

        /* We assume 10 bits per character.  This may not be exactly
         * accurate depending on settings, but it should be good enough. */
        s->ns_per_char = ns_per_bit * 10;
    }

#ifdef DEBUG_STM32_UART
    DPRINTF("%s clock is set to %lu Hz.\n",
                stm32_periph_name(s->periph),
                (unsigned long)clk_freq);
    DPRINTF("%s BRR set to %lu.\n",
                stm32_periph_name(s->periph),
                (unsigned long)s->USART_BRR);
    DPRINTF("%s Baud is set to %lu bits per sec.\n",
                stm32_periph_name(s->periph),
                (unsigned long)s->bits_per_sec);
#endif
}

/* Handle a change in the peripheral clock. */
static void stm32_uart_clk_irq_handler(void *opaque, int n, int level)
{
    Stm32Uart *s = (Stm32Uart *)opaque;

    assert(n == 0);

    /* Only update the BAUD rate if the IRQ is being set. */
    if(level) {
        stm32_uart_baud_update(s);
    }
}

/* Routine which updates the USART's IRQ.  This should be called whenever
 * an interrupt-related flag is updated.
 */
static void stm32_uart_update_irq(Stm32Uart *s) {
    /* Note that we are not checking the ORE flag, but we should be. */
    int new_irq_level =
       (s->USART_CR1_TCIE & s->USART_SR_TC) |
       (s->USART_CR1_TXEIE & s->USART_SR_TXE) |
       (s->USART_CR1_RXNEIE &
               (s->USART_SR_ORE | s->USART_SR_RXNE));

    /* Only trigger an interrupt if the IRQ level changes.  We probably could
     * set the level regardless, but we will just check for good measure.
     */
    if(new_irq_level ^ s->curr_irq_level) {
        qemu_set_irq(s->irq, new_irq_level);
        s->curr_irq_level = new_irq_level;
    }
}


static void stm32_uart_start_tx(Stm32Uart *s, uint32_t value);

/* Routine to be called when a transmit is complete. */
static void stm32_uart_tx_complete(Stm32Uart *s)
{
    if(s->USART_SR_TXE == 1) {
        /* If the buffer is empty, there is nothing waiting to be transmitted.
         * Mark the transmit complete. */
        s->USART_SR_TC = 1;
        stm32_uart_update_irq(s);
    } else {
        /* Otherwise, mark the transmit buffer as empty and
         * start transmitting the value stored there.
         */
        s->USART_SR_TXE = 1;
        stm32_uart_update_irq(s);
        stm32_uart_start_tx(s, s->USART_TDR);
    }
}

/* Start transmitting a character. */
static void stm32_uart_start_tx(Stm32Uart *s, uint32_t value)
{
    uint64_t curr_time = qemu_get_clock_ns(vm_clock);
    uint8_t ch = value; //This will truncate the ninth bit

    /* Reset the Transmission Complete flag to indicate a transmit is in
     * progress.
     */
    s->USART_SR_TC = 0;

    /* Write the character out. */
    if (s->chr) {
        qemu_chr_fe_write(s->chr, &ch, 1);
    }
#ifdef STM32_UART_NO_BAUD_DELAY
    /* If BAUD delays are not being simulated, then immediately mark the
     * transmission as complete.
     */
    curr_time = curr_time; //Avoid "variable unused" compiler error
    stm32_uart_tx_complete(s);
#else
    /* Otherwise, start the transmit delay timer. */
    qemu_mod_timer(s->tx_timer,  curr_time + s->ns_per_char);
#endif
}

/* Checks the USART transmit pin's GPIO settings.  If the GPIO is not configured
 * properly, a hardware error is triggered.
 */
static void stm32_uart_check_tx_pin(Stm32Uart *s)
{
    int tx_periph, tx_pin;
    int config;

    switch(s->periph) {
        case STM32_UART1:
            switch(stm32_afio_get_periph_map(s->stm32_afio, s->periph)) {
                case STM32_USART1_NO_REMAP:
                    tx_periph = STM32_GPIOA;
                    tx_pin = 9;
                    break;
                case STM32_USART1_REMAP:
                    tx_periph = STM32_GPIOB;
                    tx_pin = 6;
                    break;
                default:
                    assert(false);
                    break;
            }
            break;
        case STM32_UART2:
            switch(stm32_afio_get_periph_map(s->stm32_afio, s->periph)) {
                case STM32_USART2_NO_REMAP:
                    tx_periph = STM32_GPIOA;
                    tx_pin = 2;
                    break;
                case STM32_USART2_REMAP:
                    tx_periph = STM32_GPIOD;
                    tx_pin = 5;
                    break;
                default:
                    assert(false);
                    break;
            }
            break;
        case STM32_UART3:
            switch(stm32_afio_get_periph_map(s->stm32_afio, s->periph)) {
                case STM32_USART3_NO_REMAP:
                    tx_periph = STM32_GPIOB;
                    tx_pin = 10;
                    break;
                case STM32_USART3_PARTIAL_REMAP:
                    tx_periph = STM32_GPIOC;
                    tx_pin = 10;
                    break;
                case STM32_USART3_FULL_REMAP:
                    tx_periph = STM32_GPIOD;
                    tx_pin = 8;
                    break;
                default:
                    assert(false);
                    break;
            }
            break;
        default:
            assert(false);
            break;
    }

    Stm32Gpio *gpio_dev = s->stm32_gpio[STM32_GPIO_INDEX_FROM_PERIPH(tx_periph)];

    if(stm32_gpio_get_mode_bits(gpio_dev, tx_pin) == STM32_GPIO_MODE_IN) {
        hw_error("UART TX pin needs to be configured as output");
    }

    config = stm32_gpio_get_config_bits(gpio_dev, tx_pin);
    if((config != STM32_GPIO_OUT_ALT_PUSHPULL) &&
            (config != STM32_GPIO_OUT_ALT_OPEN)) {
        hw_error("UART TX pin needs to be configured as "
                 "alternate function output");
    }
}





/* TIMER HANDLERS */
/* Once the receive delay is finished, indicate the USART is finished receiving.
 * This will allow it to receive the next character.  The current character was
 * already received before starting the delay.
 */
static void stm32_uart_rx_timer_expire(void *opaque) {
    Stm32Uart *s = (Stm32Uart *)opaque;

    s->receiving = false;
}

/* When the transmit delay is complete, mark the transmit as complete
 * (the character was already sent before starting the delay). */
static void stm32_uart_tx_timer_expire(void *opaque) {
    Stm32Uart *s = (Stm32Uart *)opaque;

    stm32_uart_tx_complete(s);
}





/* CHAR DEVICE HANDLERS */

static int stm32_uart_can_receive(void *opaque)
{
    Stm32Uart *s = (Stm32Uart *)opaque;

    if(s->USART_CR1_UE && s->USART_CR1_RE) {
        /* The USART can only receive if it is enabled. */
        if(s->receiving) {
            /* If the USART is already receiving, then it cannot receive another
             * character yet.
             */
            return 0;
        } else {
#ifdef STM32_UART_ENABLE_OVERRUN
            /* If overrun is enabled, then always allow the next character to be
             * received even if the buffer already has a value.  This is how
             * real hardware behaves.*/
            return 1;
#else
            /* Otherwise, do not allow the next character to be received until
             * software has read the previous one.
             */
            if(s->USART_SR_RXNE) {
                return 0;
            } else {
                return 1;
            }
#endif
        }
    } else {
        /* Always allow a character to be received if the module is disabled.
         * However, the character will just be ignored (just like on real
         * hardware). */
        return 1;
    }
}

static void stm32_uart_event(void *opaque, int event)
{
    /* Do nothing */
}

static void stm32_uart_receive(void *opaque, const uint8_t *buf, int size)
{
    Stm32Uart *s = (Stm32Uart *)opaque;
    uint64_t curr_time = qemu_get_clock_ns(vm_clock);

    assert(size > 0);

    /* Only handle the received character if the module is enabled, */
    if(s->USART_CR1_UE && s->USART_CR1_RE) {
        /* If there is already a character in the receive buffer, then
         * set the overflow flag.
         */
        if(s->USART_SR_RXNE) {
            s->USART_SR_ORE = 1;
            s->sr_read_since_ore_set = false;
            stm32_uart_update_irq(s);
        }

        /* Receive the character and mark the buffer as not empty. */
        s->USART_RDR = *buf;
        s->USART_SR_RXNE = 1;
        stm32_uart_update_irq(s);
    }

#ifdef STM32_UART_NO_BAUD_DELAY
    /* Do nothing - there is no delay before the module reports it can receive
     * the next character. */
    curr_time = curr_time; //Avoid "variable unused" compiler error
#else
    /* Indicate the module is receiving and start the delay. */
    s->receiving = true;
    qemu_mod_timer(s->rx_timer,  curr_time + s->ns_per_char);
#endif
}




/* REGISTER IMPLEMENTATION */

static uint32_t stm32_uart_USART_SR_read(Stm32Uart *s)
{
    /* If the Overflow flag is set, reading the SR register is the first step
     * to resetting the flag.
     */
    if(s->USART_SR_ORE) {
        s->sr_read_since_ore_set = true;
    }

    return (s->USART_SR_TXE << USART_SR_TXE_BIT) |
           (s->USART_SR_TC << USART_SR_TC_BIT) |
           (s->USART_SR_RXNE << USART_SR_RXNE_BIT) |
           (s->USART_SR_ORE << USART_SR_ORE_BIT);
}





static void stm32_uart_USART_SR_write(Stm32Uart *s, uint32_t new_value)
{
    uint32_t new_TC, new_RXNE;

    new_TC = GET_BIT_VALUE(new_value, USART_SR_TC_BIT);
    /* The Transmit Complete flag can be cleared, but not set. */
    if(new_TC) {
        hw_error("Software attempted to set USART TC bit\n");
    }
    s->USART_SR_TC = new_TC;

    new_RXNE = GET_BIT_VALUE(new_value, USART_SR_RXNE_BIT);
    /* The Read Data Register Not Empty flag can be cleared, but not set. */
    if(new_RXNE) {
        hw_error("Software attempted to set USART RXNE bit\n");
    }
    s->USART_SR_RXNE = new_RXNE;

    stm32_uart_update_irq(s);
}


static void stm32_uart_USART_DR_read(Stm32Uart *s, uint32_t *read_value)
{
    /* If the Overflow flag is set, then it should be cleared if the software
     * performs an SR read followed by a DR read.
     */
    if(s->USART_SR_ORE) {
        if(s->sr_read_since_ore_set) {
            s->USART_SR_ORE = 0;

        }
    }

    if(!s->USART_CR1_UE) {
        hw_error("Attempted to read from USART_DR while UART was disabled.");
    }

    if(!s->USART_CR1_RE) {
        hw_error("Attempted to read from USART_DR while UART receiver "
                 "was disabled.");
    }

    if(s->USART_SR_RXNE) {
        /* If the receive buffer is not empty, return the value. and mark the
         * buffer as empty.
         */
        (*read_value) = s->USART_RDR;
        s->USART_SR_RXNE = 0;
    } else {
        hw_error("Read value from USART_DR while it was empty.");
    }

    stm32_uart_update_irq(s);
}


static void stm32_uart_USART_DR_write(Stm32Uart *s, uint32_t new_value)
{
    uint32_t write_value = new_value & 0x000001ff;

    if(!s->USART_CR1_UE) {
        hw_error("Attempted to write to USART_DR while UART was disabled.");
    }

    if(!s->USART_CR1_TE) {
        hw_error("Attempted to write to USART_DR while UART transmitter "
                 "was disabled.");
    }

    stm32_uart_check_tx_pin(s);

    if(s->USART_SR_TC) {
        /* If the Transmission Complete bit is set, it means the USART is not
         * currently transmitting.  This means, a transmission can immediately
         * start.
         */
        stm32_uart_start_tx(s, write_value);
    } else {
        /* Otherwise check to see if the buffer is empty.
         * If it is, then store the new character there and mark it as not empty.
         * If it is not empty, trigger a hardware error.  Software should check
         * to make sure it is empty before writing to the Data Register.
         */
        if(s->USART_SR_TXE) {
            s->USART_TDR = write_value;
            s->USART_SR_TXE = 0;
        } else {
            hw_error("Wrote new value to USART_DR while it was non-empty.");
        }
    }

    stm32_uart_update_irq(s);
}

/* Update the Baud Rate Register. */
static void stm32_uart_USART_BRR_write(Stm32Uart *s, uint32_t new_value,
                                        bool init)
{
    s->USART_BRR = new_value & 0x0000ffff;

    stm32_uart_baud_update(s);
}

static void stm32_uart_USART_CR1_write(Stm32Uart *s, uint32_t new_value,
                                        bool init)
{
    s->USART_CR1_UE = GET_BIT_VALUE(new_value, USART_CR1_UE_BIT);
    if(s->USART_CR1_UE) {
        /* Check to make sure the correct mapping is selected when enabling the
         * USART.
         */
        if(s->afio_board_map != stm32_afio_get_periph_map(s->stm32_afio, s->periph)) {
            hw_error("Bad AFIO mapping for %s", stm32_periph_name(s->periph));
        }
    }

    s->USART_CR1_TXEIE = GET_BIT_VALUE(new_value, USART_CR1_TXEIE_BIT);
    s->USART_CR1_TCIE = GET_BIT_VALUE(new_value, USART_CR1_TCIE_BIT);
    s->USART_CR1_RXNEIE = GET_BIT_VALUE(new_value, USART_CR1_RXNEIE_BIT);

    s->USART_CR1_TE = GET_BIT_VALUE(new_value, USART_CR1_TE_BIT);
    s->USART_CR1_RE = GET_BIT_VALUE(new_value, USART_CR1_RE_BIT);

    s->USART_CR1 = new_value & 0x00003fff;

    stm32_uart_update_irq(s);
}

static void stm32_uart_USART_CR2_write(Stm32Uart *s, uint32_t new_value,
                                        bool init)
{
    s->USART_CR2 = new_value & 0x00007f7f;
}

static void stm32_uart_USART_CR3_write(Stm32Uart *s, uint32_t new_value,
                                        bool init)
{
    s->USART_CR3 = new_value & 0x000007ff;
}

static void stm32_uart_reset(DeviceState *dev)
{
    Stm32Uart *s = FROM_SYSBUS(Stm32Uart, sysbus_from_qdev(dev));

    /* Initialize the status registers.  These are mostly
     * read-only, so we do not call the "write" routine
     * like normal.
     */
    s->USART_SR_TXE = 1;
    s->USART_SR_TC = 1;
    s->USART_SR_RXNE = 0;
    s->USART_SR_ORE = 0;

    // Do not initialize USART_DR - it is documented as undefined at reset
    // and does not behave like normal registers.
    stm32_uart_USART_BRR_write(s, 0x00000000, true);
    stm32_uart_USART_CR1_write(s, 0x00000000, true);
    stm32_uart_USART_CR2_write(s, 0x00000000, true);
    stm32_uart_USART_CR3_write(s, 0x00000000, true);

    stm32_uart_update_irq(s);
}


static uint64_t stm32_uart_readw(Stm32Uart *s, target_phys_addr_t offset)
{
    uint32_t value;

    switch (offset) {
        case USART_SR_OFFSET:
            return stm32_uart_USART_SR_read(s);
        case USART_DR_OFFSET:
            stm32_uart_USART_DR_read(s, &value);
            return value;
        case USART_BRR_OFFSET:
            return s->USART_BRR;
        case USART_CR1_OFFSET:
            return s->USART_CR1;
        case USART_CR2_OFFSET:
            return s->USART_CR2;
        case USART_CR3_OFFSET:
            return s->USART_CR3;
        case USART_GTPR_OFFSET:
            STM32_NOT_IMPL_REG(offset, 4);
            return 0;
        default:
            STM32_BAD_REG(offset, 4);
            return 0;
    }
}

static void stm32_uart_writew(
        Stm32Uart *s,
        target_phys_addr_t offset,
        uint64_t value)
{
    switch (offset) {
        case USART_SR_OFFSET:
            stm32_uart_USART_SR_write(s, value);
            break;
        case USART_DR_OFFSET:
            stm32_uart_USART_DR_write(s, value);
            break;
        case USART_BRR_OFFSET:
            stm32_uart_USART_BRR_write(s, value, false);
            break;
        case USART_CR1_OFFSET:
            stm32_uart_USART_CR1_write(s, value, false);
            break;
        case USART_CR2_OFFSET:
            stm32_uart_USART_CR2_write(s, value, false);
            break;
        case USART_CR3_OFFSET:
            stm32_uart_USART_CR3_write(s, value, false);
            break;
        case USART_GTPR_OFFSET:
            STM32_NOT_IMPL_REG(offset, 4);
            break;
        default:
            STM32_BAD_REG(offset, 4);
            break;
    }
}


static uint64_t stm32_uart_readh(Stm32Uart *s, target_phys_addr_t offset)
{
    uint32_t value;

    switch (offset) {
        case USART_SR_OFFSET:
            return STM32_REG_READH_VALUE(offset, stm32_uart_USART_SR_read(s));
        case USART_DR_OFFSET:
            stm32_uart_USART_DR_read(s, &value);
            return STM32_REG_READH_VALUE(offset, value);
        case USART_BRR_OFFSET:
            return STM32_REG_READH_VALUE(offset, s->USART_BRR);
        case USART_CR1_OFFSET:
            return STM32_REG_READH_VALUE(offset, s->USART_CR1);
        case USART_CR2_OFFSET:
            return STM32_REG_READH_VALUE(offset, s->USART_CR2);
        case USART_CR3_OFFSET:
            return STM32_REG_READH_VALUE(offset, s->USART_CR3);
        case USART_GTPR_OFFSET:
            STM32_NOT_IMPL_REG(offset, 2);
            return 0;
        default:
            STM32_BAD_REG(offset, 2);
            return 0;
    }
}

static void stm32_uart_writeh(
        Stm32Uart *s,
        target_phys_addr_t offset,
        uint64_t value)
{
    switch (offset) {
        case USART_SR_OFFSET:
            /* The SR register only has bits in the first halfword, so no need to do
             * anything special.
             */
            stm32_uart_USART_SR_write(s, value);
            break;
        case USART_DR_OFFSET:
            stm32_uart_USART_DR_write(s,
                    STM32_REG_WRITEH_VALUE(offset, 0, value));
            break;
        case USART_BRR_OFFSET:
            stm32_uart_USART_BRR_write(s,
                    STM32_REG_WRITEH_VALUE(offset, s->USART_BRR, value), false);
            break;
        case USART_CR1_OFFSET:
            stm32_uart_USART_CR1_write(s,
                    STM32_REG_WRITEH_VALUE(offset, s->USART_CR1, value), false);
            break;
        case USART_CR2_OFFSET:
            stm32_uart_USART_CR2_write(s,
                    STM32_REG_WRITEH_VALUE(offset, s->USART_CR2, value), false);
            break;
        case USART_CR3_OFFSET:
            stm32_uart_USART_CR3_write(s,
                    STM32_REG_WRITEH_VALUE(offset, s->USART_CR3, value), false);
            break;
        case USART_GTPR_OFFSET:
            STM32_NOT_IMPL_REG(offset, 2);
            break;
        default:
            STM32_BAD_REG(offset, 2);
            break;
    }
}

static uint64_t stm32_uart_read(void *opaque, target_phys_addr_t offset,
                          unsigned size)
{
    Stm32Uart *s = (Stm32Uart *)opaque;

    switch(size) {
        case HALFWORD_ACCESS_SIZE:
            return stm32_uart_readh(s, offset);
        case WORD_ACCESS_SIZE:
            return stm32_uart_readw(s, offset);
        default:
            STM32_BAD_REG(offset, size);
            return 0;
    }
}

static void stm32_uart_write(void *opaque, target_phys_addr_t offset,
                       uint64_t value, unsigned size)
{
    Stm32Uart *s = (Stm32Uart *)opaque;

    stm32_rcc_check_periph_clk((Stm32Rcc *)s->stm32_rcc, s->periph);

    switch(size) {
        case HALFWORD_ACCESS_SIZE:
            stm32_uart_writeh(s, offset, value);
            break;
        case WORD_ACCESS_SIZE:
            stm32_uart_writew(s, offset, value);
            break;
        default:
            STM32_BAD_REG(offset, size);
            break;
    }
}

static const MemoryRegionOps stm32_uart_ops = {
    .read = stm32_uart_read,
    .write = stm32_uart_write,
    .endianness = DEVICE_NATIVE_ENDIAN
};





/* PUBLIC FUNCTIONS */

void stm32_uart_connect(Stm32Uart *s, CharDriverState *chr,
                        uint32_t afio_board_map)
{
    s->chr = chr;
    if (chr) {
        qemu_chr_add_handlers(
                s->chr,
                stm32_uart_can_receive,
                stm32_uart_receive,
                stm32_uart_event,
                (void *)s);
    }

    s->afio_board_map = afio_board_map;
}




/* DEVICE INITIALIZATION */

static int stm32_uart_init(SysBusDevice *dev)
{
    qemu_irq *clk_irq;
    Stm32Uart *s = FROM_SYSBUS(Stm32Uart, dev);

    s->stm32_rcc = (Stm32Rcc *)s->stm32_rcc_prop;
    s->stm32_gpio = (Stm32Gpio **)s->stm32_gpio_prop;
    s->stm32_afio = (Stm32Afio *)s->stm32_afio_prop;

    memory_region_init_io(&s->iomem, &stm32_uart_ops, s,
                          "uart", 0x03ff);
    sysbus_init_mmio(dev, &s->iomem);

    sysbus_init_irq(dev, &s->irq);

    s->rx_timer =
          qemu_new_timer_ns(vm_clock,(QEMUTimerCB *)stm32_uart_rx_timer_expire, s);
    s->tx_timer =
          qemu_new_timer_ns(vm_clock,(QEMUTimerCB *)stm32_uart_tx_timer_expire, s);

    /* Register handlers to handle updates to the USART's peripheral clock. */
    clk_irq =
          qemu_allocate_irqs(stm32_uart_clk_irq_handler, (void *)s, 1);
    stm32_rcc_set_periph_clk_irq(s->stm32_rcc, s->periph, clk_irq[0]);

    stm32_uart_reset((DeviceState *)s);

    return 0;
}

static Property stm32_uart_properties[] = {
    DEFINE_PROP_PERIPH_T("periph", Stm32Uart, periph, STM32_PERIPH_UNDEFINED),
    DEFINE_PROP_PTR("stm32_rcc", Stm32Uart, stm32_rcc_prop),
    DEFINE_PROP_PTR("stm32_gpio", Stm32Uart, stm32_gpio_prop),
    DEFINE_PROP_PTR("stm32_afio", Stm32Uart, stm32_afio_prop),
    DEFINE_PROP_END_OF_LIST()
};

static void stm32_uart_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = stm32_uart_init;
    dc->reset = stm32_uart_reset;
    dc->props = stm32_uart_properties;
}

static TypeInfo stm32_uart_info = {
    .name  = "stm32_uart",
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(Stm32Uart),
    .class_init = stm32_uart_class_init
};

static void stm32_uart_register_types(void)
{
    type_register_static(&stm32_uart_info);
}

type_init(stm32_uart_register_types)
