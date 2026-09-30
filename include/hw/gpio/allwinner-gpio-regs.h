/*
 *  Allwinner GPIO registers definition
 *
 *  Copyright (C) 2026 Strahinja Jankovic. <strahinja.p.jankovic@gmail.com>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef ALLWINNER_GPIO_REGS_H
#define ALLWINNER_GPIO_REGS_H

/* GPIO ports index (n) */
#define GPIO_PA     0
#define GPIO_PB     1
#define GPIO_PC     2
#define GPIO_PD     3
#define GPIO_PE     4
#define GPIO_PF     5
#define GPIO_PG     6
#define GPIO_PH     7
#define GPIO_PI     8

#ifndef AW_GPIO_PORTS_NUM
#define AW_GPIO_PORTS_NUM 9
#endif

#ifndef AW_GPIO_PIN_COUNT
#define AW_GPIO_PIN_COUNT 32
#endif

typedef struct AWPortMap {
    uint32_t cfg[4];
    uint32_t dat;
    uint32_t drv[2];
    uint32_t pul[2];
} AWPortMap;

typedef struct AWPortsOverlay {
    AWPortMap ports[AW_GPIO_PORTS_NUM];
} AWPortsOverlay;

static const uint32_t AW_PINS_PER_PORT[AW_GPIO_PORTS_NUM] = {
    18,
    24,
    25,
    28,
    12,
    6,
    12,
    28,
    22
};

typedef struct {
    uint32_t drv[2];
    uint32_t pul[2];
} AWPortResetVals;

static const AWPortResetVals aw_gpio_port_reset[] = {
    [GPIO_PA] = { .drv = { 0x55555555, 0x00000005 } },
    [GPIO_PB] = { .drv = { 0x55555555, 0x00005555 } },
    [GPIO_PC] = { .drv = { 0x55555555, 0x00015555 },
                  .pul = { 0x00005140, 0x00004016 } },
    [GPIO_PD] = { .drv = { 0x55555555, 0x00555555 } },
    [GPIO_PE] = { .drv = { 0x00555555, 0x0 } },
    [GPIO_PF] = { .drv = { 0x00000555, 0x0 } },
    [GPIO_PG] = { .drv = { 0x05555555, 0x0 } },
    [GPIO_PH] = { .drv = { 0x55555555, 0x00555555 } },
    [GPIO_PI] = { .drv = { 0x55555555, 0x00000555 } },
};

#define DEFAULT_CFG_MASK    0x77777777
#define DEFAULT_DRV_MASK    0xffffffff
#define DEFAULT_PUL_MASK    0xffffffff

#define CFG_INPUT_MASK      0x0
#define CFG_OUTPUT_MASK     0x1
#define CFG_IO_MASK         0x1
#define CFG_PIN_STRIDE      4
#define CFG_PINS_PER_REG    (AW_GPIO_PIN_COUNT / CFG_PIN_STRIDE)

#define AW_INT_GPIO_COUNT   32
#define INT_CFG_IRQ_STRIDE  4
#define INT_CFG_IRQ_PER_REG (AW_INT_GPIO_COUNT / INT_CFG_IRQ_STRIDE)

/* GPIO masks per number of pins */
#define GPIO_CFG0_PINS_MASK(pins) \
    ((pins >= CFG_PINS_PER_REG) ? DEFAULT_CFG_MASK : \
    ((1 << (pins * CFG_PIN_STRIDE)) - 1) & DEFAULT_CFG_MASK)

#define GPIO_CFG1_PINS_MASK(pins) \
    ((pins >= 16) ? DEFAULT_CFG_MASK : \
    ((pins > 8) ? ((1 << ((pins - 8) * 4)) - 1) & DEFAULT_CFG_MASK : 0))

#define GPIO_CFG2_PINS_MASK(pins) \
    ((pins >= 24) ? DEFAULT_CFG_MASK : \
    ((pins > 16) ? ((1 << ((pins - 16) * 4)) - 1) & DEFAULT_CFG_MASK : 0))

#define GPIO_CFG3_PINS_MASK(pins) \
    ((pins > 24) ? ((1 << ((pins - 24) * 4)) - 1) & DEFAULT_CFG_MASK : 0)

#define GPIO_DAT_PINS_MASK(pins)    ((1 << pins) - 1)

#define GPIO_DRV0_PINS_MASK(pins) \
    ((pins > 16) ? DEFAULT_DRV_MASK : ((1 << (pins * 2)) - 1))

#define GPIO_DRV1_PINS_MASK(pins) \
    ((pins > 16) ? ((1 << ((pins - 16) * 2)) - 1) : 0)

#define GPIO_PUL0_PINS_MASK(pins) \
    ((pins > 16) ? DEFAULT_PUL_MASK : ((1 << (pins * 2)) - 1))

#define GPIO_PUL1_PINS_MASK(pins) \
    ((pins > 16) ? ((1 << ((pins - 16) * 2)) - 1) : 0)


#define PORT_STRIDE     0x24

/* Allwinner GPIO memory map */
#define CFG0    0x00 /* Configure register 0 */
#define CFG1    0x04 /* Configure register 1 */
#define CFG2    0x08 /* Configure register 2 */
#define CFG3    0x0c /* Configure register 3 */
#define DAT     0x10 /* Data register */
#define DRV0    0x14 /* Multi-driving register 0 */
#define DRV1    0x18 /* Multi-driving register 1 */
#define PUL0    0x1c /* Pull register 0 */
#define PUL1    0x20 /* Pull register 1 */

/* Port n configure register 0 */
#define GPIO_Pn_CFG0(n)   (n*PORT_STRIDE + CFG0)
/* Port n configure register 1 */
#define GPIO_Pn_CFG1(n)   (n*PORT_STRIDE + CFG1)
/* Port n configure register 2 */
#define GPIO_Pn_CFG2(n)   (n*PORT_STRIDE + CFG2)
/* Port n configure register 3 */
#define GPIO_Pn_CFG3(n)   (n*PORT_STRIDE + CFG3)
/* Port n data register */
#define GPIO_Pn_DAT(n)    (n*PORT_STRIDE + DAT)
/* Port n Multi-driving register 0 */
#define GPIO_Pn_DRV0(n)   (n*PORT_STRIDE + DRV0)
/* Port n Multi-driving register 1 */
#define GPIO_Pn_DRV1(n)   (n*PORT_STRIDE + DRV1)
/* Port n Pull register 0 */
#define GPIO_Pn_PUL0(n)   (n*PORT_STRIDE + PUL0)
/* Port n Pull register 1 */
#define GPIO_Pn_PUL1(n)   (n*PORT_STRIDE + PUL1)
/* PIO interrupt configure register 0 */
#define GPIO_INT_CFG0       0x200
/* PIO interrupt configure register 1 */
#define GPIO_INT_CFG1       0x204
/* PIO interrupt configure register 2 */
#define GPIO_INT_CFG2       0x208
/* PIO interrupt configure register 3 */
#define GPIO_INT_CFG3       0x20c
/* PIO interrupt control register */
#define GPIO_INT_CTL        0x210
/* PIO interrupt status register */
#define GPIO_INT_STA        0x214
/* PIO interrupt debounce register */
#define GPIO_INT_DEB        0x218
/* SDRAM Pad Multi-driving register */
#define SDR_PAD_DRV         0x220
/* SDRAM Pad Pull register */
#define SDR_PAD_PUL         0x224

typedef enum AWGPIOLevel {
    AW_GPIO_LEVEL_LOW = 0,
    AW_GPIO_LEVEL_HIGH = 1,
} AWGPIOLevel;

typedef enum AWGPIOCfg {
    AW_GPIO_CFG_IN = 0,
    AW_GPIO_CFG_OUT = 1,
    AW_GPIO_CFG_FUN = 2,
} AWGPIOCfg;

typedef enum AWGPIOIrqCfg {
    AW_GPIO_IRQ_CFG_RISING_EDGE = 0,
    AW_GPIO_IRQ_CFG_FALLING_EDGE = 1,
    AW_GPIO_IRQ_CFG_HIGH_LEVEL = 2,
    AW_GPIO_IRQ_CFG_LOW_LEVEL = 3,
    AW_GPIO_IRQ_CFG_BOTH_EDGE = 4
} AWGPIOIrqCfg;

static const char *const port_name_table[] = {
    "PA", "PB", "PC", "PD", "PE", "PF", "PG", "PH", "PI"
};

static const char *portname(unsigned index)
{
    return index < AW_GPIO_PORTS_NUM ? port_name_table[index] : "?";
}

static inline char *portname_in(int port)
{
    return g_strconcat(portname(port), "_IN", NULL);
}

static inline char *portname_out(int port)
{
    return g_strconcat(portname(port), "_OUT", NULL);
}

static int irq_nr(int port, int line)
{
    int irq = -1;

    switch (port) {
    case GPIO_PH:
        if (line <= 21) {
            irq = line;
        }
        break;
    case GPIO_PI:
        if ((line >= 10) && (line <= 19)) {
            irq = line + 12;
        }
        break;
    default:
        break;
    }
    return irq;
}


#endif /* ALLWINNER_GPIO_REGS_H */
