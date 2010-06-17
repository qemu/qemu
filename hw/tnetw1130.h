/*
 * QEMU emulation for Texas Instruments TNETW1130 (ACX111) wireless.
 *
 * Copyright (C) 2007-2010 Stefan Weil
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

/* Total number of memory and I/O regions. */
#define  TNETW1130_REGIONS      2

/* 2 memory regions. */
#define TNETW1130_MEM0_SIZE      (8 * KiB)
#define TNETW1130_MEM1_SIZE      (128 * KiB)

/* No I/O regions. */
//~ #define TNETW1130_IO_SIZE      (0 * KiB)

#define TNETW1130_FW_SIZE        (128 * KiB)

typedef enum {
    TNETW1130_SOFT_RESET = 0x0000,
    TNETW1130_SLV_MEM_ADDR = 0x0014,
    TNETW1130_SLV_MEM_DATA = 0x0018,
    TNETW1130_SLV_MEM_CTL = 0x001c,
    TNETW1130_SLV_END_CTL = 0x0020,
    TNETW1130_FEMR = 0x0034,
    TNETW1130_INT_TRIG = 0x00b4,
    TNETW1130_IRQ_MASK = 0x00d4,
    TNETW1130_IRQ_STATUS_CLEAR = 0x00e4,
    TNETW1130_IRQ_ACK = 0x00e8,
    TNETW1130_HINT_TRIG = 0x00ec,
    TNETW1130_IRQ_STATUS_NON_DES = 0x00f0,
    TNETW1130_EE_START = 0x0100,
    TNETW1130_SOR_CFG = 0x0104,
    TNETW1130_ECPU_CTRL = 0x0108,
    TNETW1130_ENABLE = 0x01d0,
    TNETW1130_EEPROM_CTL = 0x0338,
    TNETW1130_EEPROM_ADDR = 0x033c,
    TNETW1130_EEPROM_DATA = 0x0340,
    TNETW1130_EEPROM_CFG = 0x0344,
    TNETW1130_PHY_ADDR = 0x0350,
    TNETW1130_PHY_DATA = 0x0354,
    TNETW1130_PHY_CTL = 0x0358,
    TNETW1130_GPIO_OE = 0x0374,
    TNETW1130_GPIO_OUT = 0x037c,
    TNETW1130_CMD_MAILBOX_OFFS = 0x0388,
    TNETW1130_INFO_MAILBOX_OFFS = 0x038c,
    TNETW1130_EEPROM_INFORMATION = 0x390,
} tnetw1130_reg_t;

typedef struct {
    /* Variables for QEMU interface. */

    /* Handles for memory mapped I/O. */
    int io_memory[TNETW1130_REGIONS];

    /* region addresses. */
    uint32_t region[TNETW1130_REGIONS];

    //~ eeprom_t *eeprom;

    uint16_t irq_status;

    NICConf conf;
    NICState *nic;
    uint8_t mem0[TNETW1130_MEM0_SIZE];
    uint8_t mem1[TNETW1130_MEM1_SIZE];
    uint32_t fw_addr;
    uint8_t fw[TNETW1130_FW_SIZE];
    //~ uint8_t filter[1024];
    //~ uint32_t silicon_revision;
} tnetw1130_t;

extern CPUReadMemoryFunc *tnetw1130_region0_read[];
extern CPUReadMemoryFunc *tnetw1130_region1_read[];
extern CPUWriteMemoryFunc *tnetw1130_region0_write[];
extern CPUWriteMemoryFunc *tnetw1130_region1_write[];
