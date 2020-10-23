/*
 *  LatticeMico32 hwsetup helper functions.
 *
 *  Copyright (c) 2010 Michael Walle <michael@walle.cc>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * These are helper functions for creating the hardware description blob used
 * in the Theobroma's uClinux port.
 */

#ifndef QEMU_HW_LM32_HWSETUP_H
#define QEMU_HW_LM32_HWSETUP_H

#include "qemu/cutils.h"
#include "hw/loader.h"

typedef struct {
    void *data;
    void *ptr;
} HWSetup;

enum hwsetup_tag {
    HWSETUP_TAG_EOL         = 0,
    HWSETUP_TAG_CPU         = 1,
    HWSETUP_TAG_ASRAM       = 2,
    HWSETUP_TAG_FLASH       = 3,
    HWSETUP_TAG_SDRAM       = 4,
    HWSETUP_TAG_OCM         = 5,
    HWSETUP_TAG_DDR_SDRAM   = 6,
    HWSETUP_TAG_DDR2_SDRAM  = 7,
    HWSETUP_TAG_TIMER       = 8,
    HWSETUP_TAG_UART        = 9,
    HWSETUP_TAG_GPIO        = 10,
    HWSETUP_TAG_TRISPEEDMAC = 11,
    HWSETUP_TAG_I2CM        = 12,
    HWSETUP_TAG_LEDS        = 13,
    HWSETUP_TAG_7SEG        = 14,
    HWSETUP_TAG_SPI_S       = 15,
    HWSETUP_TAG_SPI_M       = 16,
};

static inline HWSetup *hwsetup_init(void)
{
    HWSetup *hw;

    hw = g_malloc(sizeof(HWSetup));
    hw->data = g_malloc0(TARGET_PAGE_SIZE);
    hw->ptr = hw->data;

    return hw;
}

static inline void hwsetup_free(HWSetup *hw)
{
    g_free(hw->data);
    g_free(hw);
}

static inline void hwsetup_create_rom(HWSetup *hw,
        hwaddr base)
{
    rom_add_blob("hwsetup", hw->data, TARGET_PAGE_SIZE,
                 TARGET_PAGE_SIZE, base, NULL, NULL, NULL, NULL, true);
}

static inline void hwsetup_add_u8(HWSetup *hw, uint8_t u)
{
    stb_p(hw->ptr, u);
    hw->ptr += 1;
}

static inline void hwsetup_add_u32(HWSetup *hw, uint32_t u)
{
    stl_p(hw->ptr, u);
    hw->ptr += 4;
}

static inline void hwsetup_add_tag(HWSetup *hw, enum hwsetup_tag t)
{
    stl_p(hw->ptr, t);
    hw->ptr += 4;
}

static inline void hwsetup_add_str(HWSetup *hw, const char *str)
{
    pstrcpy(hw->ptr, 32, str);
    hw->ptr += 32;
}

static inline void hwsetup_add_trailer(HWSetup *hw)
{
    hwsetup_add_u32(hw, 8); /* size */
    hwsetup_add_tag(hw, HWSETUP_TAG_EOL);
}

static inline void hwsetup_add_cpu(HWSetup *hw,
        const char *name, uint32_t frequency)
{
    hwsetup_add_u32(hw, 44); /* size */
    hwsetup_add_tag(hw, HWSETUP_TAG_CPU);
    hwsetup_add_str(hw, name);
    hwsetup_add_u32(hw, frequency);
}

static inline void hwsetup_add_flash(HWSetup *hw,
        const char *name, uint32_t base, uint32_t size)
{
    hwsetup_add_u32(hw, 52); /* size */
    hwsetup_add_tag(hw, HWSETUP_TAG_FLASH);
    hwsetup_add_str(hw, name);
    hwsetup_add_u32(hw, base);
    hwsetup_add_u32(hw, size);
    hwsetup_add_u8(hw, 8); /* read latency */
    hwsetup_add_u8(hw, 8); /* write latency */
    hwsetup_add_u8(hw, 25); /* address width */
    hwsetup_add_u8(hw, 32); /* data width */
}

static inline void hwsetup_add_ddr_sdram(HWSetup *hw,
        const char *name, uint32_t base, uint32_t size)
{
    hwsetup_add_u32(hw, 48); /* size */
    hwsetup_add_tag(hw, HWSETUP_TAG_DDR_SDRAM);
    hwsetup_add_str(hw, name);
    hwsetup_add_u32(hw, base);
    hwsetup_add_u32(hw, size);
}

static inline void hwsetup_add_timer(HWSetup *hw,
        const char *name, uint32_t base, uint32_t irq)
{
    hwsetup_add_u32(hw, 56); /* size */
    hwsetup_add_tag(hw, HWSETUP_TAG_TIMER);
    hwsetup_add_str(hw, name);
    hwsetup_add_u32(hw, base);
    hwsetup_add_u8(hw, 1); /* wr_tickcount */
    hwsetup_add_u8(hw, 1); /* rd_tickcount */
    hwsetup_add_u8(hw, 1); /* start_stop_control */
    hwsetup_add_u8(hw, 32); /* counter_width */
    hwsetup_add_u32(hw, 20); /* reload_ticks */
    hwsetup_add_u8(hw, irq);
    hwsetup_add_u8(hw, 0); /* padding */
    hwsetup_add_u8(hw, 0); /* padding */
    hwsetup_add_u8(hw, 0); /* padding */
}

static inline void hwsetup_add_uart(HWSetup *hw,
        const char *name, uint32_t base, uint32_t irq)
{
    hwsetup_add_u32(hw, 56); /* size */
    hwsetup_add_tag(hw, HWSETUP_TAG_UART);
    hwsetup_add_str(hw, name);
    hwsetup_add_u32(hw, base);
    hwsetup_add_u32(hw, 115200); /* baudrate */
    hwsetup_add_u8(hw, 8); /* databits */
    hwsetup_add_u8(hw, 1); /* stopbits */
    hwsetup_add_u8(hw, 1); /* use_interrupt */
    hwsetup_add_u8(hw, 1); /* block_on_transmit */
    hwsetup_add_u8(hw, 1); /* block_on_receive */
    hwsetup_add_u8(hw, 4); /* rx_buffer_size */
    hwsetup_add_u8(hw, 4); /* tx_buffer_size */
    hwsetup_add_u8(hw, irq);
}

#endif /* QEMU_HW_LM32_HWSETUP_H */
