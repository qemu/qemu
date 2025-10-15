/*
 *  ASPEED GPIO Controller
 *
 *  Copyright (C) 2017-2019 IBM Corp.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/host-utils.h"
#include "qemu/log.h"
#include "hw/gpio/aspeed_gpio.h"
#include "hw/misc/aspeed_scu.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "trace.h"
#include "hw/registerfields.h"

#define GPIOS_PER_GROUP 8

/* GPIO Source Types */
#define ASPEED_CMD_SRC_MASK         0x01010101
#define ASPEED_SOURCE_ARM           0
#define ASPEED_SOURCE_LPC           1
#define ASPEED_SOURCE_COPROCESSOR   2
#define ASPEED_SOURCE_RESERVED      3

/* GPIO Interrupt Triggers */
/*
 *  For each set of gpios there are three sensitivity registers that control
 *  the interrupt trigger mode.
 *
 *  | 2 | 1 | 0 | trigger mode
 *  -----------------------------
 *  | 0 | 0 | 0 | falling-edge
 *  | 0 | 0 | 1 | rising-edge
 *  | 0 | 1 | 0 | level-low
 *  | 0 | 1 | 1 | level-high
 *  | 1 | X | X | dual-edge
 */
#define ASPEED_FALLING_EDGE 0
#define ASPEED_RISING_EDGE  1
#define ASPEED_LEVEL_LOW    2
#define ASPEED_LEVEL_HIGH   3
#define ASPEED_DUAL_EDGE    4

/* GPIO Register Address Offsets */
#define GPIO_ABCD_DATA_VALUE       (0x000 >> 2)
#define GPIO_ABCD_DIRECTION        (0x004 >> 2)
#define GPIO_ABCD_INT_ENABLE       (0x008 >> 2)
#define GPIO_ABCD_INT_SENS_0       (0x00C >> 2)
#define GPIO_ABCD_INT_SENS_1       (0x010 >> 2)
#define GPIO_ABCD_INT_SENS_2       (0x014 >> 2)
#define GPIO_ABCD_INT_STATUS       (0x018 >> 2)
#define GPIO_ABCD_RESET_TOLERANT   (0x01C >> 2)
#define GPIO_EFGH_DATA_VALUE       (0x020 >> 2)
#define GPIO_EFGH_DIRECTION        (0x024 >> 2)
#define GPIO_EFGH_INT_ENABLE       (0x028 >> 2)
#define GPIO_EFGH_INT_SENS_0       (0x02C >> 2)
#define GPIO_EFGH_INT_SENS_1       (0x030 >> 2)
#define GPIO_EFGH_INT_SENS_2       (0x034 >> 2)
#define GPIO_EFGH_INT_STATUS       (0x038 >> 2)
#define GPIO_EFGH_RESET_TOLERANT   (0x03C >> 2)
#define GPIO_ABCD_DEBOUNCE_1       (0x040 >> 2)
#define GPIO_ABCD_DEBOUNCE_2       (0x044 >> 2)
#define GPIO_EFGH_DEBOUNCE_1       (0x048 >> 2)
#define GPIO_EFGH_DEBOUNCE_2       (0x04C >> 2)
#define GPIO_DEBOUNCE_TIME_1       (0x050 >> 2)
#define GPIO_DEBOUNCE_TIME_2       (0x054 >> 2)
#define GPIO_DEBOUNCE_TIME_3       (0x058 >> 2)
#define GPIO_ABCD_COMMAND_SRC_0    (0x060 >> 2)
#define GPIO_ABCD_COMMAND_SRC_1    (0x064 >> 2)
#define GPIO_EFGH_COMMAND_SRC_0    (0x068 >> 2)
#define GPIO_EFGH_COMMAND_SRC_1    (0x06C >> 2)
#define GPIO_IJKL_DATA_VALUE       (0x070 >> 2)
#define GPIO_IJKL_DIRECTION        (0x074 >> 2)
#define GPIO_MNOP_DATA_VALUE       (0x078 >> 2)
#define GPIO_MNOP_DIRECTION        (0x07C >> 2)
#define GPIO_QRST_DATA_VALUE       (0x080 >> 2)
#define GPIO_QRST_DIRECTION        (0x084 >> 2)
#define GPIO_UVWX_DATA_VALUE       (0x088 >> 2)
#define GPIO_UVWX_DIRECTION        (0x08C >> 2)
#define GPIO_IJKL_COMMAND_SRC_0    (0x090 >> 2)
#define GPIO_IJKL_COMMAND_SRC_1    (0x094 >> 2)
#define GPIO_IJKL_INT_ENABLE       (0x098 >> 2)
#define GPIO_IJKL_INT_SENS_0       (0x09C >> 2)
#define GPIO_IJKL_INT_SENS_1       (0x0A0 >> 2)
#define GPIO_IJKL_INT_SENS_2       (0x0A4 >> 2)
#define GPIO_IJKL_INT_STATUS       (0x0A8 >> 2)
#define GPIO_IJKL_RESET_TOLERANT   (0x0AC >> 2)
#define GPIO_IJKL_DEBOUNCE_1       (0x0B0 >> 2)
#define GPIO_IJKL_DEBOUNCE_2       (0x0B4 >> 2)
#define GPIO_IJKL_INPUT_MASK       (0x0B8 >> 2)
#define GPIO_ABCD_DATA_READ        (0x0C0 >> 2)
#define GPIO_EFGH_DATA_READ        (0x0C4 >> 2)
#define GPIO_IJKL_DATA_READ        (0x0C8 >> 2)
#define GPIO_MNOP_DATA_READ        (0x0CC >> 2)
#define GPIO_QRST_DATA_READ        (0x0D0 >> 2)
#define GPIO_UVWX_DATA_READ        (0x0D4 >> 2)
#define GPIO_YZAAAB_DATA_READ      (0x0D8 >> 2)
#define GPIO_AC_DATA_READ          (0x0DC >> 2)
#define GPIO_MNOP_COMMAND_SRC_0    (0x0E0 >> 2)
#define GPIO_MNOP_COMMAND_SRC_1    (0x0E4 >> 2)
#define GPIO_MNOP_INT_ENABLE       (0x0E8 >> 2)
#define GPIO_MNOP_INT_SENS_0       (0x0EC >> 2)
#define GPIO_MNOP_INT_SENS_1       (0x0F0 >> 2)
#define GPIO_MNOP_INT_SENS_2       (0x0F4 >> 2)
#define GPIO_MNOP_INT_STATUS       (0x0F8 >> 2)
#define GPIO_MNOP_RESET_TOLERANT   (0x0FC >> 2)
#define GPIO_MNOP_DEBOUNCE_1       (0x100 >> 2)
#define GPIO_MNOP_DEBOUNCE_2       (0x104 >> 2)
#define GPIO_MNOP_INPUT_MASK       (0x108 >> 2)
#define GPIO_QRST_COMMAND_SRC_0    (0x110 >> 2)
#define GPIO_QRST_COMMAND_SRC_1    (0x114 >> 2)
#define GPIO_QRST_INT_ENABLE       (0x118 >> 2)
#define GPIO_QRST_INT_SENS_0       (0x11C >> 2)
#define GPIO_QRST_INT_SENS_1       (0x120 >> 2)
#define GPIO_QRST_INT_SENS_2       (0x124 >> 2)
#define GPIO_QRST_INT_STATUS       (0x128 >> 2)
#define GPIO_QRST_RESET_TOLERANT   (0x12C >> 2)
#define GPIO_QRST_DEBOUNCE_1       (0x130 >> 2)
#define GPIO_QRST_DEBOUNCE_2       (0x134 >> 2)
#define GPIO_QRST_INPUT_MASK       (0x138 >> 2)
#define GPIO_UVWX_COMMAND_SRC_0    (0x140 >> 2)
#define GPIO_UVWX_COMMAND_SRC_1    (0x144 >> 2)
#define GPIO_UVWX_INT_ENABLE       (0x148 >> 2)
#define GPIO_UVWX_INT_SENS_0       (0x14C >> 2)
#define GPIO_UVWX_INT_SENS_1       (0x150 >> 2)
#define GPIO_UVWX_INT_SENS_2       (0x154 >> 2)
#define GPIO_UVWX_INT_STATUS       (0x158 >> 2)
#define GPIO_UVWX_RESET_TOLERANT   (0x15C >> 2)
#define GPIO_UVWX_DEBOUNCE_1       (0x160 >> 2)
#define GPIO_UVWX_DEBOUNCE_2       (0x164 >> 2)
#define GPIO_UVWX_INPUT_MASK       (0x168 >> 2)
#define GPIO_YZAAAB_COMMAND_SRC_0  (0x170 >> 2)
#define GPIO_YZAAAB_COMMAND_SRC_1  (0x174 >> 2)
#define GPIO_YZAAAB_INT_ENABLE     (0x178 >> 2)
#define GPIO_YZAAAB_INT_SENS_0     (0x17C >> 2)
#define GPIO_YZAAAB_INT_SENS_1     (0x180 >> 2)
#define GPIO_YZAAAB_INT_SENS_2     (0x184 >> 2)
#define GPIO_YZAAAB_INT_STATUS     (0x188 >> 2)
#define GPIO_YZAAAB_RESET_TOLERANT (0x18C >> 2)
#define GPIO_YZAAAB_DEBOUNCE_1     (0x190 >> 2)
#define GPIO_YZAAAB_DEBOUNCE_2     (0x194 >> 2)
#define GPIO_YZAAAB_INPUT_MASK     (0x198 >> 2)
#define GPIO_AC_COMMAND_SRC_0      (0x1A0 >> 2)
#define GPIO_AC_COMMAND_SRC_1      (0x1A4 >> 2)
#define GPIO_AC_INT_ENABLE         (0x1A8 >> 2)
#define GPIO_AC_INT_SENS_0         (0x1AC >> 2)
#define GPIO_AC_INT_SENS_1         (0x1B0 >> 2)
#define GPIO_AC_INT_SENS_2         (0x1B4 >> 2)
#define GPIO_AC_INT_STATUS         (0x1B8 >> 2)
#define GPIO_AC_RESET_TOLERANT     (0x1BC >> 2)
#define GPIO_AC_DEBOUNCE_1         (0x1C0 >> 2)
#define GPIO_AC_DEBOUNCE_2         (0x1C4 >> 2)
#define GPIO_AC_INPUT_MASK         (0x1C8 >> 2)
#define GPIO_ABCD_INPUT_MASK       (0x1D0 >> 2)
#define GPIO_EFGH_INPUT_MASK       (0x1D4 >> 2)
#define GPIO_YZAAAB_DATA_VALUE     (0x1E0 >> 2)
#define GPIO_YZAAAB_DIRECTION      (0x1E4 >> 2)
#define GPIO_AC_DATA_VALUE         (0x1E8 >> 2)
#define GPIO_AC_DIRECTION          (0x1EC >> 2)
#define GPIO_3_3V_MEM_SIZE         0x1F0
#define GPIO_3_3V_REG_ARRAY_SIZE   (GPIO_3_3V_MEM_SIZE >> 2)

/* AST2600 only - 1.8V gpios */
/*
 * The AST2600 two copies of the GPIO controller: the same 3.3V gpios as the
 * AST2400 (memory offsets 0x0-0x198) and a second controller with 1.8V gpios
 * (memory offsets 0x800-0x9D4).
 */
#define GPIO_1_8V_ABCD_DATA_VALUE     (0x000 >> 2)
#define GPIO_1_8V_ABCD_DIRECTION      (0x004 >> 2)
#define GPIO_1_8V_ABCD_INT_ENABLE     (0x008 >> 2)
#define GPIO_1_8V_ABCD_INT_SENS_0     (0x00C >> 2)
#define GPIO_1_8V_ABCD_INT_SENS_1     (0x010 >> 2)
#define GPIO_1_8V_ABCD_INT_SENS_2     (0x014 >> 2)
#define GPIO_1_8V_ABCD_INT_STATUS     (0x018 >> 2)
#define GPIO_1_8V_ABCD_RESET_TOLERANT (0x01C >> 2)
#define GPIO_1_8V_E_DATA_VALUE        (0x020 >> 2)
#define GPIO_1_8V_E_DIRECTION         (0x024 >> 2)
#define GPIO_1_8V_E_INT_ENABLE        (0x028 >> 2)
#define GPIO_1_8V_E_INT_SENS_0        (0x02C >> 2)
#define GPIO_1_8V_E_INT_SENS_1        (0x030 >> 2)
#define GPIO_1_8V_E_INT_SENS_2        (0x034 >> 2)
#define GPIO_1_8V_E_INT_STATUS        (0x038 >> 2)
#define GPIO_1_8V_E_RESET_TOLERANT    (0x03C >> 2)
#define GPIO_1_8V_ABCD_DEBOUNCE_1     (0x040 >> 2)
#define GPIO_1_8V_ABCD_DEBOUNCE_2     (0x044 >> 2)
#define GPIO_1_8V_E_DEBOUNCE_1        (0x048 >> 2)
#define GPIO_1_8V_E_DEBOUNCE_2        (0x04C >> 2)
#define GPIO_1_8V_DEBOUNCE_TIME_1     (0x050 >> 2)
#define GPIO_1_8V_DEBOUNCE_TIME_2     (0x054 >> 2)
#define GPIO_1_8V_DEBOUNCE_TIME_3     (0x058 >> 2)
#define GPIO_1_8V_ABCD_COMMAND_SRC_0  (0x060 >> 2)
#define GPIO_1_8V_ABCD_COMMAND_SRC_1  (0x064 >> 2)
#define GPIO_1_8V_E_COMMAND_SRC_0     (0x068 >> 2)
#define GPIO_1_8V_E_COMMAND_SRC_1     (0x06C >> 2)
#define GPIO_1_8V_ABCD_DATA_READ      (0x0C0 >> 2)
#define GPIO_1_8V_E_DATA_READ         (0x0C4 >> 2)
#define GPIO_1_8V_ABCD_INPUT_MASK     (0x1D0 >> 2)
#define GPIO_1_8V_E_INPUT_MASK        (0x1D4 >> 2)
#define GPIO_1_8V_MEM_SIZE            0x1D8
#define GPIO_1_8V_REG_ARRAY_SIZE      (GPIO_1_8V_MEM_SIZE >> 2)

/*
 * GPIO index mode support
 * It only supports write operation
 */
REG32(GPIO_INDEX_REG, 0x2AC)
    FIELD(GPIO_INDEX_REG, NUMBER, 0, 8)
    FIELD(GPIO_INDEX_REG, COMMAND, 12, 1)
    FIELD(GPIO_INDEX_REG, TYPE, 16, 4)
    FIELD(GPIO_INDEX_REG, DATA_VALUE, 20, 1)
    FIELD(GPIO_INDEX_REG, DIRECTION, 20, 1)
    FIELD(GPIO_INDEX_REG, INT_ENABLE, 20, 1)
    FIELD(GPIO_INDEX_REG, INT_SENS_0, 21, 1)
    FIELD(GPIO_INDEX_REG, INT_SENS_1, 22, 1)
    FIELD(GPIO_INDEX_REG, INT_SENS_2, 23, 1)
    FIELD(GPIO_INDEX_REG, INT_STATUS, 24, 1)
    FIELD(GPIO_INDEX_REG, DEBOUNCE_1, 20, 1)
    FIELD(GPIO_INDEX_REG, DEBOUNCE_2, 21, 1)
    FIELD(GPIO_INDEX_REG, RESET_TOLERANT, 20, 1)
    FIELD(GPIO_INDEX_REG, COMMAND_SRC_0, 20, 1)
    FIELD(GPIO_INDEX_REG, COMMAND_SRC_1, 21, 1)
    FIELD(GPIO_INDEX_REG, INPUT_MASK, 20, 1)

/* AST2700 GPIO Register Address Offsets */
REG32(GPIO_2700_DEBOUNCE_TIME_1, 0x000)
REG32(GPIO_2700_DEBOUNCE_TIME_2, 0x004)
REG32(GPIO_2700_DEBOUNCE_TIME_3, 0x008)
REG32(GPIO_2700_INT_STATUS_1, 0x100)
REG32(GPIO_2700_INT_STATUS_2, 0x104)
REG32(GPIO_2700_INT_STATUS_3, 0x108)
REG32(GPIO_2700_INT_STATUS_4, 0x10C)
REG32(GPIO_2700_INT_STATUS_5, 0x110)
REG32(GPIO_2700_INT_STATUS_6, 0x114)
REG32(GPIO_2700_INT_STATUS_7, 0x118)
/* GPIOA0 - GPIOAA7 Control Register */
REG32(GPIO_A0_CONTROL, 0x180)
    SHARED_FIELD(GPIO_CONTROL_OUT_DATA, 0, 1)
    SHARED_FIELD(GPIO_CONTROL_DIRECTION, 1, 1)
    SHARED_FIELD(GPIO_CONTROL_INT_ENABLE, 2, 1)
    SHARED_FIELD(GPIO_CONTROL_INT_SENS_0, 3, 1)
    SHARED_FIELD(GPIO_CONTROL_INT_SENS_1, 4, 1)
    SHARED_FIELD(GPIO_CONTROL_INT_SENS_2, 5, 1)
    SHARED_FIELD(GPIO_CONTROL_RESET_TOLERANCE, 6, 1)
    SHARED_FIELD(GPIO_CONTROL_DEBOUNCE_1, 7, 1)
    SHARED_FIELD(GPIO_CONTROL_DEBOUNCE_2, 8, 1)
    SHARED_FIELD(GPIO_CONTROL_INPUT_MASK, 9, 1)
    SHARED_FIELD(GPIO_CONTROL_BLINK_COUNTER_1, 10, 1)
    SHARED_FIELD(GPIO_CONTROL_BLINK_COUNTER_2, 11, 1)
    SHARED_FIELD(GPIO_CONTROL_INT_STATUS, 12, 1)
    SHARED_FIELD(GPIO_CONTROL_IN_DATA, 13, 1)
    SHARED_FIELD(GPIO_CONTROL_RESERVED, 14, 18)
REG32(GPIO_AA7_CONTROL, 0x4DC)
#define GPIO_2700_MEM_SIZE 0x4E0
#define GPIO_2700_REG_ARRAY_SIZE (GPIO_2700_MEM_SIZE >> 2)

static int aspeed_evaluate_irq(GPIOSets *regs, int gpio_prev_high, int gpio)
{
    uint32_t falling_edge = 0, rising_edge = 0;
    uint32_t int_trigger = extract32(regs->int_sens_0, gpio, 1)
                           | extract32(regs->int_sens_1, gpio, 1) << 1
                           | extract32(regs->int_sens_2, gpio, 1) << 2;
    uint32_t gpio_curr_high = extract32(regs->data_value, gpio, 1);
    uint32_t gpio_int_enabled = extract32(regs->int_enable, gpio, 1);

    if (!gpio_int_enabled) {
        return 0;
    }

    /* Detect edges */
    if (gpio_curr_high && !gpio_prev_high) {
        rising_edge = 1;
    } else if (!gpio_curr_high && gpio_prev_high) {
        falling_edge = 1;
    }

    if (((int_trigger == ASPEED_FALLING_EDGE)  && falling_edge)  ||
        ((int_trigger == ASPEED_RISING_EDGE)  && rising_edge)    ||
        ((int_trigger == ASPEED_LEVEL_LOW)  && !gpio_curr_high)  ||
        ((int_trigger == ASPEED_LEVEL_HIGH)  && gpio_curr_high)  ||
        ((int_trigger >= ASPEED_DUAL_EDGE)  && (rising_edge || falling_edge)))
    {
        regs->int_status = deposit32(regs->int_status, gpio, 1, 1);
        return 1;
    }
    return 0;
}

#define nested_struct_index(ta, pa, m, tb, pb) \
        (pb - ((tb *)(((char *)pa) + offsetof(ta, m))))

static ptrdiff_t aspeed_gpio_set_idx(AspeedGPIOState *s, GPIOSets *regs)
{
    return nested_struct_index(AspeedGPIOState, s, sets, GPIOSets, regs);
}

static void aspeed_gpio_update(AspeedGPIOState *s, GPIOSets *regs,
                               uint32_t value, uint32_t mode_mask)
{
    uint32_t input_mask = regs->input_mask;
    uint32_t direction = regs->direction;
    uint32_t old = regs->data_value;
    uint32_t new = value;
    uint32_t diff;
    int gpio;

    diff = (old ^ new);
    diff &= mode_mask;
    if (diff) {
        for (gpio = 0; gpio < ASPEED_GPIOS_PER_SET; gpio++) {
            uint32_t mask = 1U << gpio;

            /* If the gpio needs to be updated... */
            if (!(diff & mask)) {
                continue;
            }

            /* ...and we're output or not input-masked... */
            if (!(direction & mask) && (input_mask & mask)) {
                continue;
            }

            /* ...then update the state. */
            if (mask & new) {
                regs->data_value |= mask;
            } else {
                regs->data_value &= ~mask;
            }

            /* If the gpio is set to output... */
            if (direction & mask) {
                /* ...trigger the line-state IRQ */
                ptrdiff_t set = aspeed_gpio_set_idx(s, regs);
                qemu_set_irq(s->gpios[set][gpio], !!(new & mask));
            } else {
                /* ...otherwise if we meet the line's current IRQ policy... */
                if (aspeed_evaluate_irq(regs, old & mask, gpio)) {
                    /* ...trigger the VIC IRQ */
                    s->pending++;
                }
            }
        }
    }
    qemu_set_irq(s->irq, !!(s->pending));
}

static bool aspeed_gpio_get_pin_level(AspeedGPIOState *s, uint32_t set_idx,
                                      uint32_t pin)
{
    uint32_t reg_val;
    uint32_t pin_mask = 1 << pin;

    reg_val = s->sets[set_idx].data_value;

    return !!(reg_val & pin_mask);
}

static void aspeed_gpio_set_pin_level(AspeedGPIOState *s, uint32_t set_idx,
                                      uint32_t pin, bool level)
{
    uint32_t value = s->sets[set_idx].data_value;
    uint32_t pin_mask = 1 << pin;

    if (level) {
        value |= pin_mask;
    } else {
        value &= ~pin_mask;
    }

    aspeed_gpio_update(s, &s->sets[set_idx], value,
                       ~s->sets[set_idx].direction);
}

/*
 *  | src_1 | src_2 |  source     |
 *  |-----------------------------|
 *  |   0   |   0   |  ARM        |
 *  |   0   |   1   |  LPC        |
 *  |   1   |   0   |  Coprocessor|
 *  |   1   |   1   |  Reserved   |
 *
 *  Once the source of a set is programmed, corresponding bits in the
 *  data_value, direction, interrupt [enable, sens[0-2]], reset_tol and
 *  debounce registers can only be written by the source.
 *
 *  Source is ARM by default
 *  only bits 24, 16, 8, and 0 can be set
 *
 *  we don't currently have a model for the LPC or Coprocessor
 */
static uint32_t update_value_control_source(GPIOSets *regs, uint32_t old_value,
                                            uint32_t value)
{
    int i;
    int cmd_source;

    /* assume the source is always ARM for now */
    int source = ASPEED_SOURCE_ARM;

    uint32_t new_value = 0;

    /* for each group in set */
    for (i = 0; i < ASPEED_GPIOS_PER_SET; i += GPIOS_PER_GROUP) {
        cmd_source = extract32(regs->cmd_source_0, i, 1)
                | (extract32(regs->cmd_source_1, i, 1) << 1);

        if (source == cmd_source) {
            new_value |= (0xff << i) & value;
        } else {
            new_value |= (0xff << i) & old_value;
        }
    }
    return new_value;
}

static const AspeedGPIOReg aspeed_3_3v_gpios[GPIO_3_3V_REG_ARRAY_SIZE] = {
    /* Set ABCD */
    [GPIO_ABCD_DATA_VALUE] =     { 0, gpio_reg_data_value },
    [GPIO_ABCD_DIRECTION] =      { 0, gpio_reg_direction },
    [GPIO_ABCD_INT_ENABLE] =     { 0, gpio_reg_int_enable },
    [GPIO_ABCD_INT_SENS_0] =     { 0, gpio_reg_int_sens_0 },
    [GPIO_ABCD_INT_SENS_1] =     { 0, gpio_reg_int_sens_1 },
    [GPIO_ABCD_INT_SENS_2] =     { 0, gpio_reg_int_sens_2 },
    [GPIO_ABCD_INT_STATUS] =     { 0, gpio_reg_int_status },
    [GPIO_ABCD_RESET_TOLERANT] = { 0, gpio_reg_reset_tolerant },
    [GPIO_ABCD_DEBOUNCE_1] =     { 0, gpio_reg_debounce_1 },
    [GPIO_ABCD_DEBOUNCE_2] =     { 0, gpio_reg_debounce_2 },
    [GPIO_ABCD_COMMAND_SRC_0] =  { 0, gpio_reg_cmd_source_0 },
    [GPIO_ABCD_COMMAND_SRC_1] =  { 0, gpio_reg_cmd_source_1 },
    [GPIO_ABCD_DATA_READ] =      { 0, gpio_reg_data_read },
    [GPIO_ABCD_INPUT_MASK] =     { 0, gpio_reg_input_mask },
    /* Set EFGH */
    [GPIO_EFGH_DATA_VALUE] =     { 1, gpio_reg_data_value },
    [GPIO_EFGH_DIRECTION] =      { 1, gpio_reg_direction },
    [GPIO_EFGH_INT_ENABLE] =     { 1, gpio_reg_int_enable },
    [GPIO_EFGH_INT_SENS_0] =     { 1, gpio_reg_int_sens_0 },
    [GPIO_EFGH_INT_SENS_1] =     { 1, gpio_reg_int_sens_1 },
    [GPIO_EFGH_INT_SENS_2] =     { 1, gpio_reg_int_sens_2 },
    [GPIO_EFGH_INT_STATUS] =     { 1, gpio_reg_int_status },
    [GPIO_EFGH_RESET_TOLERANT] = { 1, gpio_reg_reset_tolerant },
    [GPIO_EFGH_DEBOUNCE_1] =     { 1, gpio_reg_debounce_1 },
    [GPIO_EFGH_DEBOUNCE_2] =     { 1, gpio_reg_debounce_2 },
    [GPIO_EFGH_COMMAND_SRC_0] =  { 1, gpio_reg_cmd_source_0 },
    [GPIO_EFGH_COMMAND_SRC_1] =  { 1, gpio_reg_cmd_source_1 },
    [GPIO_EFGH_DATA_READ] =      { 1, gpio_reg_data_read },
    [GPIO_EFGH_INPUT_MASK] =     { 1, gpio_reg_input_mask },
    /* Set IJKL */
    [GPIO_IJKL_DATA_VALUE] =     { 2, gpio_reg_data_value },
    [GPIO_IJKL_DIRECTION] =      { 2, gpio_reg_direction },
    [GPIO_IJKL_INT_ENABLE] =     { 2, gpio_reg_int_enable },
    [GPIO_IJKL_INT_SENS_0] =     { 2, gpio_reg_int_sens_0 },
    [GPIO_IJKL_INT_SENS_1] =     { 2, gpio_reg_int_sens_1 },
    [GPIO_IJKL_INT_SENS_2] =     { 2, gpio_reg_int_sens_2 },
    [GPIO_IJKL_INT_STATUS] =     { 2, gpio_reg_int_status },
    [GPIO_IJKL_RESET_TOLERANT] = { 2, gpio_reg_reset_tolerant },
    [GPIO_IJKL_DEBOUNCE_1] =     { 2, gpio_reg_debounce_1 },
    [GPIO_IJKL_DEBOUNCE_2] =     { 2, gpio_reg_debounce_2 },
    [GPIO_IJKL_COMMAND_SRC_0] =  { 2, gpio_reg_cmd_source_0 },
    [GPIO_IJKL_COMMAND_SRC_1] =  { 2, gpio_reg_cmd_source_1 },
    [GPIO_IJKL_DATA_READ] =      { 2, gpio_reg_data_read },
    [GPIO_IJKL_INPUT_MASK] =     { 2, gpio_reg_input_mask },
    /* Set MNOP */
    [GPIO_MNOP_DATA_VALUE] =     { 3, gpio_reg_data_value },
    [GPIO_MNOP_DIRECTION] =      { 3, gpio_reg_direction },
    [GPIO_MNOP_INT_ENABLE] =     { 3, gpio_reg_int_enable },
    [GPIO_MNOP_INT_SENS_0] =     { 3, gpio_reg_int_sens_0 },
    [GPIO_MNOP_INT_SENS_1] =     { 3, gpio_reg_int_sens_1 },
    [GPIO_MNOP_INT_SENS_2] =     { 3, gpio_reg_int_sens_2 },
    [GPIO_MNOP_INT_STATUS] =     { 3, gpio_reg_int_status },
    [GPIO_MNOP_RESET_TOLERANT] = { 3, gpio_reg_reset_tolerant },
    [GPIO_MNOP_DEBOUNCE_1] =     { 3, gpio_reg_debounce_1 },
    [GPIO_MNOP_DEBOUNCE_2] =     { 3, gpio_reg_debounce_2 },
    [GPIO_MNOP_COMMAND_SRC_0] =  { 3, gpio_reg_cmd_source_0 },
    [GPIO_MNOP_COMMAND_SRC_1] =  { 3, gpio_reg_cmd_source_1 },
    [GPIO_MNOP_DATA_READ] =      { 3, gpio_reg_data_read },
    [GPIO_MNOP_INPUT_MASK] =     { 3, gpio_reg_input_mask },
    /* Set QRST */
    [GPIO_QRST_DATA_VALUE] =     { 4, gpio_reg_data_value },
    [GPIO_QRST_DIRECTION] =      { 4, gpio_reg_direction },
    [GPIO_QRST_INT_ENABLE] =     { 4, gpio_reg_int_enable },
    [GPIO_QRST_INT_SENS_0] =     { 4, gpio_reg_int_sens_0 },
    [GPIO_QRST_INT_SENS_1] =     { 4, gpio_reg_int_sens_1 },
    [GPIO_QRST_INT_SENS_2] =     { 4, gpio_reg_int_sens_2 },
    [GPIO_QRST_INT_STATUS] =     { 4, gpio_reg_int_status },
    [GPIO_QRST_RESET_TOLERANT] = { 4, gpio_reg_reset_tolerant },
    [GPIO_QRST_DEBOUNCE_1] =     { 4, gpio_reg_debounce_1 },
    [GPIO_QRST_DEBOUNCE_2] =     { 4, gpio_reg_debounce_2 },
    [GPIO_QRST_COMMAND_SRC_0] =  { 4, gpio_reg_cmd_source_0 },
    [GPIO_QRST_COMMAND_SRC_1] =  { 4, gpio_reg_cmd_source_1 },
    [GPIO_QRST_DATA_READ] =      { 4, gpio_reg_data_read },
    [GPIO_QRST_INPUT_MASK] =     { 4, gpio_reg_input_mask },
    /* Set UVWX */
    [GPIO_UVWX_DATA_VALUE] =     { 5, gpio_reg_data_value },
    [GPIO_UVWX_DIRECTION] =      { 5, gpio_reg_direction },
    [GPIO_UVWX_INT_ENABLE] =     { 5, gpio_reg_int_enable },
    [GPIO_UVWX_INT_SENS_0] =     { 5, gpio_reg_int_sens_0 },
    [GPIO_UVWX_INT_SENS_1] =     { 5, gpio_reg_int_sens_1 },
    [GPIO_UVWX_INT_SENS_2] =     { 5, gpio_reg_int_sens_2 },
    [GPIO_UVWX_INT_STATUS] =     { 5, gpio_reg_int_status },
    [GPIO_UVWX_RESET_TOLERANT] = { 5, gpio_reg_reset_tolerant },
    [GPIO_UVWX_DEBOUNCE_1] =     { 5, gpio_reg_debounce_1 },
    [GPIO_UVWX_DEBOUNCE_2] =     { 5, gpio_reg_debounce_2 },
    [GPIO_UVWX_COMMAND_SRC_0] =  { 5, gpio_reg_cmd_source_0 },
    [GPIO_UVWX_COMMAND_SRC_1] =  { 5, gpio_reg_cmd_source_1 },
    [GPIO_UVWX_DATA_READ] =      { 5, gpio_reg_data_read },
    [GPIO_UVWX_INPUT_MASK] =     { 5, gpio_reg_input_mask },
    /* Set YZAAAB */
    [GPIO_YZAAAB_DATA_VALUE] =     { 6, gpio_reg_data_value },
    [GPIO_YZAAAB_DIRECTION] =      { 6, gpio_reg_direction },
    [GPIO_YZAAAB_INT_ENABLE] =     { 6, gpio_reg_int_enable },
    [GPIO_YZAAAB_INT_SENS_0] =     { 6, gpio_reg_int_sens_0 },
    [GPIO_YZAAAB_INT_SENS_1] =     { 6, gpio_reg_int_sens_1 },
    [GPIO_YZAAAB_INT_SENS_2] =     { 6, gpio_reg_int_sens_2 },
    [GPIO_YZAAAB_INT_STATUS] =     { 6, gpio_reg_int_status },
    [GPIO_YZAAAB_RESET_TOLERANT] = { 6, gpio_reg_reset_tolerant },
    [GPIO_YZAAAB_DEBOUNCE_1] =     { 6, gpio_reg_debounce_1 },
    [GPIO_YZAAAB_DEBOUNCE_2] =     { 6, gpio_reg_debounce_2 },
    [GPIO_YZAAAB_COMMAND_SRC_0] =  { 6, gpio_reg_cmd_source_0 },
    [GPIO_YZAAAB_COMMAND_SRC_1] =  { 6, gpio_reg_cmd_source_1 },
    [GPIO_YZAAAB_DATA_READ] =      { 6, gpio_reg_data_read },
    [GPIO_YZAAAB_INPUT_MASK] =     { 6, gpio_reg_input_mask },
    /* Set AC  (ast2500 only) */
    [GPIO_AC_DATA_VALUE] =         { 7, gpio_reg_data_value },
    [GPIO_AC_DIRECTION] =          { 7, gpio_reg_direction },
    [GPIO_AC_INT_ENABLE] =         { 7, gpio_reg_int_enable },
    [GPIO_AC_INT_SENS_0] =         { 7, gpio_reg_int_sens_0 },
    [GPIO_AC_INT_SENS_1] =         { 7, gpio_reg_int_sens_1 },
    [GPIO_AC_INT_SENS_2] =         { 7, gpio_reg_int_sens_2 },
    [GPIO_AC_INT_STATUS] =         { 7, gpio_reg_int_status },
    [GPIO_AC_RESET_TOLERANT] =     { 7, gpio_reg_reset_tolerant },
    [GPIO_AC_DEBOUNCE_1] =         { 7, gpio_reg_debounce_1 },
    [GPIO_AC_DEBOUNCE_2] =         { 7, gpio_reg_debounce_2 },
    [GPIO_AC_COMMAND_SRC_0] =      { 7, gpio_reg_cmd_source_0 },
    [GPIO_AC_COMMAND_SRC_1] =      { 7, gpio_reg_cmd_source_1 },
    [GPIO_AC_DATA_READ] =          { 7, gpio_reg_data_read },
    [GPIO_AC_INPUT_MASK] =         { 7, gpio_reg_input_mask },
};

static const AspeedGPIOReg aspeed_1_8v_gpios[GPIO_1_8V_REG_ARRAY_SIZE] = {
    /* 1.8V Set ABCD */
    [GPIO_1_8V_ABCD_DATA_VALUE] =     {0, gpio_reg_data_value},
    [GPIO_1_8V_ABCD_DIRECTION] =      {0, gpio_reg_direction},
    [GPIO_1_8V_ABCD_INT_ENABLE] =     {0, gpio_reg_int_enable},
    [GPIO_1_8V_ABCD_INT_SENS_0] =     {0, gpio_reg_int_sens_0},
    [GPIO_1_8V_ABCD_INT_SENS_1] =     {0, gpio_reg_int_sens_1},
    [GPIO_1_8V_ABCD_INT_SENS_2] =     {0, gpio_reg_int_sens_2},
    [GPIO_1_8V_ABCD_INT_STATUS] =     {0, gpio_reg_int_status},
    [GPIO_1_8V_ABCD_RESET_TOLERANT] = {0, gpio_reg_reset_tolerant},
    [GPIO_1_8V_ABCD_DEBOUNCE_1] =     {0, gpio_reg_debounce_1},
    [GPIO_1_8V_ABCD_DEBOUNCE_2] =     {0, gpio_reg_debounce_2},
    [GPIO_1_8V_ABCD_COMMAND_SRC_0] =  {0, gpio_reg_cmd_source_0},
    [GPIO_1_8V_ABCD_COMMAND_SRC_1] =  {0, gpio_reg_cmd_source_1},
    [GPIO_1_8V_ABCD_DATA_READ] =      {0, gpio_reg_data_read},
    [GPIO_1_8V_ABCD_INPUT_MASK] =     {0, gpio_reg_input_mask},
    /* 1.8V Set E */
    [GPIO_1_8V_E_DATA_VALUE] =     {1, gpio_reg_data_value},
    [GPIO_1_8V_E_DIRECTION] =      {1, gpio_reg_direction},
    [GPIO_1_8V_E_INT_ENABLE] =     {1, gpio_reg_int_enable},
    [GPIO_1_8V_E_INT_SENS_0] =     {1, gpio_reg_int_sens_0},
    [GPIO_1_8V_E_INT_SENS_1] =     {1, gpio_reg_int_sens_1},
    [GPIO_1_8V_E_INT_SENS_2] =     {1, gpio_reg_int_sens_2},
    [GPIO_1_8V_E_INT_STATUS] =     {1, gpio_reg_int_status},
    [GPIO_1_8V_E_RESET_TOLERANT] = {1, gpio_reg_reset_tolerant},
    [GPIO_1_8V_E_DEBOUNCE_1] =     {1, gpio_reg_debounce_1},
    [GPIO_1_8V_E_DEBOUNCE_2] =     {1, gpio_reg_debounce_2},
    [GPIO_1_8V_E_COMMAND_SRC_0] =  {1, gpio_reg_cmd_source_0},
    [GPIO_1_8V_E_COMMAND_SRC_1] =  {1, gpio_reg_cmd_source_1},
    [GPIO_1_8V_E_DATA_READ] =      {1, gpio_reg_data_read},
    [GPIO_1_8V_E_INPUT_MASK] =     {1, gpio_reg_input_mask},
};

static uint64_t aspeed_gpio_read(void *opaque, hwaddr offset, uint32_t size)
{
    AspeedGPIOState *s = ASPEED_GPIO(opaque);
    AspeedGPIOClass *agc = ASPEED_GPIO_GET_CLASS(s);
    uint64_t idx = -1;
    const AspeedGPIOReg *reg;
    GPIOSets *set;
    uint32_t value = 0;
    uint64_t debounce_value;

    idx = offset >> 2;
    if (idx >= GPIO_DEBOUNCE_TIME_1 && idx <= GPIO_DEBOUNCE_TIME_3) {
        idx -= GPIO_DEBOUNCE_TIME_1;
        debounce_value = (uint64_t) s->debounce_regs[idx];
        trace_aspeed_gpio_read(offset, debounce_value);
        return debounce_value;
    }

    if (idx >= agc->reg_table_count) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: idx 0x%" PRIx64 " out of bounds\n",
                      __func__, idx);
        return 0;
    }

    reg = &agc->reg_table[idx];
    if (reg->set_idx >= agc->nr_gpio_sets) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: no getter for offset 0x%"
                      PRIx64"\n", __func__, offset);
        return 0;
    }

    set = &s->sets[reg->set_idx];
    switch (reg->type) {
    case gpio_reg_data_value:
        value = set->data_value;
        break;
    case gpio_reg_direction:
        value = set->direction;
        break;
    case gpio_reg_int_enable:
        value = set->int_enable;
        break;
    case gpio_reg_int_sens_0:
        value = set->int_sens_0;
        break;
    case gpio_reg_int_sens_1:
        value = set->int_sens_1;
        break;
    case gpio_reg_int_sens_2:
        value = set->int_sens_2;
        break;
    case gpio_reg_int_status:
        value = set->int_status;
        break;
    case gpio_reg_reset_tolerant:
        value = set->reset_tol;
        break;
    case gpio_reg_debounce_1:
        value = set->debounce_1;
        break;
    case gpio_reg_debounce_2:
        value = set->debounce_2;
        break;
    case gpio_reg_cmd_source_0:
        value = set->cmd_source_0;
        break;
    case gpio_reg_cmd_source_1:
        value = set->cmd_source_1;
        break;
    case gpio_reg_data_read:
        value = set->data_read;
        break;
    case gpio_reg_input_mask:
        value = set->input_mask;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: no getter for offset 0x%"
                      PRIx64"\n", __func__, offset);
        return 0;
    }

    trace_aspeed_gpio_read(offset, value);
    return value;
}

static void aspeed_gpio_write_index_mode(void *opaque, hwaddr offset,
                                                uint64_t data, uint32_t size)
{
    AspeedGPIOState *s = ASPEED_GPIO(opaque);
    AspeedGPIOClass *agc = ASPEED_GPIO_GET_CLASS(s);
    const GPIOSetProperties *props;
    GPIOSets *set;
    uint32_t reg_idx_number = FIELD_EX32(data, GPIO_INDEX_REG, NUMBER);
    uint32_t reg_idx_type = FIELD_EX32(data, GPIO_INDEX_REG, TYPE);
    uint32_t reg_idx_command = FIELD_EX32(data, GPIO_INDEX_REG, COMMAND);
    uint32_t set_idx = reg_idx_number / ASPEED_GPIOS_PER_SET;
    uint32_t pin_idx = reg_idx_number % ASPEED_GPIOS_PER_SET;
    uint32_t group_idx = pin_idx / GPIOS_PER_GROUP;
    uint32_t reg_value = 0;
    uint32_t pending = 0;

    set = &s->sets[set_idx];
    props = &agc->props[set_idx];

    if (reg_idx_command)
        qemu_log_mask(LOG_GUEST_ERROR, "%s: offset 0x%" PRIx64 "data 0x%"
            PRIx64 "index mode wrong command 0x%x\n",
            __func__, offset, data, reg_idx_command);

    switch (reg_idx_type) {
    case gpio_reg_idx_data:
        reg_value = set->data_read;
        reg_value = deposit32(reg_value, pin_idx, 1,
                              FIELD_EX32(data, GPIO_INDEX_REG, DATA_VALUE));
        reg_value &= props->output;
        reg_value = update_value_control_source(set, set->data_value,
                                                reg_value);
        set->data_read = reg_value;
        aspeed_gpio_update(s, set, reg_value, set->direction);
        return;
    case gpio_reg_idx_direction:
        reg_value = set->direction;
        reg_value = deposit32(reg_value, pin_idx, 1,
                              FIELD_EX32(data, GPIO_INDEX_REG, DIRECTION));
        /*
         *   where data is the value attempted to be written to the pin:
         *    pin type      | input mask | output mask | expected value
         *    ------------------------------------------------------------
         *   bidirectional  |   1       |   1        |  data
         *   input only     |   1       |   0        |   0
         *   output only    |   0       |   1        |   1
         *   no pin         |   0       |   0        |   0
         *
         *  which is captured by:
         *  data = ( data | ~input) & output;
         */
        reg_value = (reg_value | ~props->input) & props->output;
        set->direction = update_value_control_source(set, set->direction,
                                                     reg_value);
        break;
    case gpio_reg_idx_interrupt:
        reg_value = set->int_enable;
        reg_value = deposit32(reg_value, pin_idx, 1,
                              FIELD_EX32(data, GPIO_INDEX_REG, INT_ENABLE));
        set->int_enable = update_value_control_source(set, set->int_enable,
                                                      reg_value);
        reg_value = set->int_sens_0;
        reg_value = deposit32(reg_value, pin_idx, 1,
                              FIELD_EX32(data, GPIO_INDEX_REG, INT_SENS_0));
        set->int_sens_0 = update_value_control_source(set, set->int_sens_0,
                                                      reg_value);
        reg_value = set->int_sens_1;
        reg_value = deposit32(reg_value, pin_idx, 1,
                              FIELD_EX32(data, GPIO_INDEX_REG, INT_SENS_1));
        set->int_sens_1 = update_value_control_source(set, set->int_sens_1,
                                                      reg_value);
        reg_value = set->int_sens_2;
        reg_value = deposit32(reg_value, pin_idx, 1,
                              FIELD_EX32(data, GPIO_INDEX_REG, INT_SENS_2));
        set->int_sens_2 = update_value_control_source(set, set->int_sens_2,
                                                      reg_value);
        /* interrupt status */
        if (FIELD_EX32(data, GPIO_INDEX_REG, INT_STATUS)) {
            /* pending is either 1 or 0 for a 1-bit field */
            pending = extract32(set->int_status, pin_idx, 1);

            assert(s->pending >= pending);

            /* No change to s->pending if pending is 0 */
            s->pending -= pending;

            /*
             * The write acknowledged the interrupt regardless of whether it
             * was pending or not. The post-condition is that it mustn't be
             * pending. Unconditionally clear the status bit.
             */
            set->int_status = deposit32(set->int_status, pin_idx, 1, 0);
        }
        break;
    case gpio_reg_idx_debounce:
        reg_value = set->debounce_1;
        reg_value = deposit32(reg_value, pin_idx, 1,
                              FIELD_EX32(data, GPIO_INDEX_REG, DEBOUNCE_1));
        set->debounce_1 = update_value_control_source(set, set->debounce_1,
                                                      reg_value);
        reg_value = set->debounce_2;
        reg_value = deposit32(reg_value, pin_idx, 1,
                              FIELD_EX32(data, GPIO_INDEX_REG, DEBOUNCE_2));
        set->debounce_2 = update_value_control_source(set, set->debounce_2,
                                                      reg_value);
        return;
    case gpio_reg_idx_tolerance:
        reg_value = set->reset_tol;
        reg_value = deposit32(reg_value, pin_idx, 1,
                              FIELD_EX32(data, GPIO_INDEX_REG, RESET_TOLERANT));
        set->reset_tol = update_value_control_source(set, set->reset_tol,
                                                     reg_value);
        return;
    case gpio_reg_idx_cmd_src:
        reg_value = set->cmd_source_0;
        reg_value = deposit32(reg_value, GPIOS_PER_GROUP * group_idx, 1,
                              FIELD_EX32(data, GPIO_INDEX_REG, COMMAND_SRC_0));
        set->cmd_source_0 = reg_value & ASPEED_CMD_SRC_MASK;
        reg_value = set->cmd_source_1;
        reg_value = deposit32(reg_value, GPIOS_PER_GROUP * group_idx, 1,
                              FIELD_EX32(data, GPIO_INDEX_REG, COMMAND_SRC_1));
        set->cmd_source_1 = reg_value & ASPEED_CMD_SRC_MASK;
        return;
    case gpio_reg_idx_input_mask:
        reg_value = set->input_mask;
        reg_value = deposit32(reg_value, pin_idx, 1,
                              FIELD_EX32(data, GPIO_INDEX_REG, INPUT_MASK));
        /*
         * feeds into interrupt generation
         * 0: read from data value reg will be updated
         * 1: read from data value reg will not be updated
         */
        set->input_mask = reg_value & props->input;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: offset 0x%" PRIx64 "data 0x%"
            PRIx64 "index mode wrong type 0x%x\n",
            __func__, offset, data, reg_idx_type);
        return;
    }
    aspeed_gpio_update(s, set, set->data_value, UINT32_MAX);
}

static void aspeed_gpio_write(void *opaque, hwaddr offset, uint64_t data,
                              uint32_t size)
{
    AspeedGPIOState *s = ASPEED_GPIO(opaque);
    AspeedGPIOClass *agc = ASPEED_GPIO_GET_CLASS(s);
    const GPIOSetProperties *props;
    uint64_t idx = -1;
    const AspeedGPIOReg *reg;
    GPIOSets *set;
    uint32_t cleared;

    trace_aspeed_gpio_write(offset, data);

    idx = offset >> 2;

    /* check gpio index mode */
    if (idx == R_GPIO_INDEX_REG) {
        aspeed_gpio_write_index_mode(opaque, offset, data, size);
        return;
    }

    if (idx >= GPIO_DEBOUNCE_TIME_1 && idx <= GPIO_DEBOUNCE_TIME_3) {
        idx -= GPIO_DEBOUNCE_TIME_1;
        s->debounce_regs[idx] = (uint32_t) data;
        return;
    }

    if (idx >= agc->reg_table_count) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: idx 0x%" PRIx64 " out of bounds\n",
                      __func__, idx);
        return;
    }

    reg = &agc->reg_table[idx];
    if (reg->set_idx >= agc->nr_gpio_sets) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: no setter for offset 0x%"
                      PRIx64"\n", __func__, offset);
        return;
    }

    set = &s->sets[reg->set_idx];
    props = &agc->props[reg->set_idx];

    switch (reg->type) {
    case gpio_reg_data_value:
        data &= props->output;
        data = update_value_control_source(set, set->data_value, data);
        set->data_read = data;
        aspeed_gpio_update(s, set, data, set->direction);
        return;
    case gpio_reg_direction:
        /*
         *   where data is the value attempted to be written to the pin:
         *    pin type      | input mask | output mask | expected value
         *    ------------------------------------------------------------
         *   bidirectional  |   1       |   1        |  data
         *   input only     |   1       |   0        |   0
         *   output only    |   0       |   1        |   1
         *   no pin         |   0       |   0        |   0
         *
         *  which is captured by:
         *  data = ( data | ~input) & output;
         */
        data = (data | ~props->input) & props->output;
        set->direction = update_value_control_source(set, set->direction, data);
        break;
    case gpio_reg_int_enable:
        set->int_enable = update_value_control_source(set, set->int_enable,
                                                      data);
        break;
    case gpio_reg_int_sens_0:
        set->int_sens_0 = update_value_control_source(set, set->int_sens_0,
                                                      data);
        break;
    case gpio_reg_int_sens_1:
        set->int_sens_1 = update_value_control_source(set, set->int_sens_1,
                                                      data);
        break;
    case gpio_reg_int_sens_2:
        set->int_sens_2 = update_value_control_source(set, set->int_sens_2,
                                                      data);
        break;
    case gpio_reg_int_status:
        cleared = ctpop32(data & set->int_status);
        if (s->pending && cleared) {
            assert(s->pending >= cleared);
            s->pending -= cleared;
        }
        set->int_status &= ~data;
        break;
    case gpio_reg_reset_tolerant:
        set->reset_tol = update_value_control_source(set, set->reset_tol,
                                                     data);
        return;
    case gpio_reg_debounce_1:
        set->debounce_1 = update_value_control_source(set, set->debounce_1,
                                                      data);
        return;
    case gpio_reg_debounce_2:
        set->debounce_2 = update_value_control_source(set, set->debounce_2,
                                                      data);
        return;
    case gpio_reg_cmd_source_0:
        set->cmd_source_0 = data & ASPEED_CMD_SRC_MASK;
        return;
    case gpio_reg_cmd_source_1:
        set->cmd_source_1 = data & ASPEED_CMD_SRC_MASK;
        return;
    case gpio_reg_data_read:
        /* Read only register */
        return;
    case gpio_reg_input_mask:
        /*
         * feeds into interrupt generation
         * 0: read from data value reg will be updated
         * 1: read from data value reg will not be updated
         */
         set->input_mask = data & props->input;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: no setter for offset 0x%"
                      PRIx64"\n", __func__, offset);
        return;
    }
    aspeed_gpio_update(s, set, set->data_value, UINT32_MAX);
}

static int get_set_idx(AspeedGPIOState *s, const char *group, int *group_idx)
{
    AspeedGPIOClass *agc = ASPEED_GPIO_GET_CLASS(s);
    int set_idx, g_idx;

    for (set_idx = 0; set_idx < agc->nr_gpio_sets; set_idx++) {
        const GPIOSetProperties *set_props = &agc->props[set_idx];
        for (g_idx = 0; g_idx < ASPEED_GROUPS_PER_SET; g_idx++) {
            if (!strncmp(group, set_props->group_label[g_idx], strlen(group))) {
                *group_idx = g_idx;
                return set_idx;
            }
        }
    }
    return -1;
}

static void aspeed_gpio_get_pin(Object *obj, Visitor *v, const char *name,
                                void *opaque, Error **errp)
{
    int pin = 0xfff;
    bool level = true;
    char group[4];
    AspeedGPIOState *s = ASPEED_GPIO(obj);
    int set_idx, group_idx = 0;

    if (sscanf(name, "gpio%2[A-Z]%1d", group, &pin) != 2) {
        /* 1.8V gpio */
        if (sscanf(name, "gpio%3[18A-E]%1d", group, &pin) != 2) {
            error_setg(errp, "%s: error reading %s", __func__, name);
            return;
        }
    }
    set_idx = get_set_idx(s, group, &group_idx);
    if (set_idx == -1) {
        error_setg(errp, "%s: invalid group %s", __func__, group);
        return;
    }
    pin =  pin + group_idx * GPIOS_PER_GROUP;
    level = aspeed_gpio_get_pin_level(s, set_idx, pin);
    visit_type_bool(v, name, &level, errp);
}

static void aspeed_gpio_set_pin(Object *obj, Visitor *v, const char *name,
                               void *opaque, Error **errp)
{
    bool level;
    int pin = 0xfff;
    char group[4];
    AspeedGPIOState *s = ASPEED_GPIO(obj);
    int set_idx, group_idx = 0;

    if (!visit_type_bool(v, name, &level, errp)) {
        return;
    }
    if (sscanf(name, "gpio%2[A-Z]%1d", group, &pin) != 2) {
        /* 1.8V gpio */
        if (sscanf(name, "gpio%3[18A-E]%1d", group, &pin) != 2) {
            error_setg(errp, "%s: error reading %s", __func__, name);
            return;
        }
    }
    set_idx = get_set_idx(s, group, &group_idx);
    if (set_idx == -1) {
        error_setg(errp, "%s: invalid group %s", __func__, group);
        return;
    }
    pin =  pin + group_idx * GPIOS_PER_GROUP;
    aspeed_gpio_set_pin_level(s, set_idx, pin, level);
}

static uint64_t aspeed_gpio_2700_read_control_reg(AspeedGPIOState *s,
                                    uint32_t pin)
{
    AspeedGPIOClass *agc = ASPEED_GPIO_GET_CLASS(s);
    GPIOSets *set;
    uint64_t value = 0;
    uint32_t set_idx;
    uint32_t pin_idx;

    set_idx = pin / ASPEED_GPIOS_PER_SET;
    pin_idx = pin % ASPEED_GPIOS_PER_SET;

    if (set_idx >= agc->nr_gpio_sets) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: set index: %d, out of bounds\n",
                      __func__, set_idx);
        return 0;
    }

    set = &s->sets[set_idx];
    value = SHARED_FIELD_DP32(value, GPIO_CONTROL_OUT_DATA,
                              extract32(set->data_read, pin_idx, 1));
    value = SHARED_FIELD_DP32(value, GPIO_CONTROL_DIRECTION,
                              extract32(set->direction, pin_idx, 1));
    value = SHARED_FIELD_DP32(value, GPIO_CONTROL_INT_ENABLE,
                              extract32(set->int_enable, pin_idx, 1));
    value = SHARED_FIELD_DP32(value, GPIO_CONTROL_INT_SENS_0,
                              extract32(set->int_sens_0, pin_idx, 1));
    value = SHARED_FIELD_DP32(value, GPIO_CONTROL_INT_SENS_1,
                              extract32(set->int_sens_1, pin_idx, 1));
    value = SHARED_FIELD_DP32(value, GPIO_CONTROL_INT_SENS_2,
                              extract32(set->int_sens_2, pin_idx, 1));
    value = SHARED_FIELD_DP32(value, GPIO_CONTROL_RESET_TOLERANCE,
                              extract32(set->reset_tol, pin_idx, 1));
    value = SHARED_FIELD_DP32(value, GPIO_CONTROL_DEBOUNCE_1,
                              extract32(set->debounce_1, pin_idx, 1));
    value = SHARED_FIELD_DP32(value, GPIO_CONTROL_DEBOUNCE_2,
                              extract32(set->debounce_2, pin_idx, 1));
    value = SHARED_FIELD_DP32(value, GPIO_CONTROL_INPUT_MASK,
                              extract32(set->input_mask, pin_idx, 1));
    value = SHARED_FIELD_DP32(value, GPIO_CONTROL_INT_STATUS,
                              extract32(set->int_status, pin_idx, 1));
    value = SHARED_FIELD_DP32(value, GPIO_CONTROL_IN_DATA,
                              extract32(set->data_value, pin_idx, 1));
    return value;
}

static void aspeed_gpio_2700_write_control_reg(AspeedGPIOState *s,
                                uint32_t pin, uint64_t data)
{
    AspeedGPIOClass *agc = ASPEED_GPIO_GET_CLASS(s);
    const GPIOSetProperties *props;
    GPIOSets *set;
    uint32_t set_idx;
    uint32_t pin_idx;
    uint32_t group_value = 0;
    uint32_t pending = 0;

    set_idx = pin / ASPEED_GPIOS_PER_SET;
    pin_idx = pin % ASPEED_GPIOS_PER_SET;

    if (set_idx >= agc->nr_gpio_sets) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: set index: %d, out of bounds\n",
                      __func__, set_idx);
        return;
    }

    set = &s->sets[set_idx];
    props = &agc->props[set_idx];

    /* direction */
    group_value = set->direction;
    group_value = deposit32(group_value, pin_idx, 1,
                            SHARED_FIELD_EX32(data, GPIO_CONTROL_DIRECTION));
    /*
     * where data is the value attempted to be written to the pin:
     * pin type      | input mask | output mask | expected value
     * ------------------------------------------------------------
     * bidirectional  |   1       |   1        |  data
     * input only     |   1       |   0        |   0
     * output only    |   0       |   1        |   1
     * no pin         |   0       |   0        |   0
     *
     * which is captured by:
     * data = ( data | ~input) & output;
     */
    group_value = (group_value | ~props->input) & props->output;
    set->direction = update_value_control_source(set, set->direction,
                                                 group_value);

    /* out data */
    group_value = set->data_read;
    group_value = deposit32(group_value, pin_idx, 1,
                            SHARED_FIELD_EX32(data, GPIO_CONTROL_OUT_DATA));
    group_value &= props->output;
    group_value = update_value_control_source(set, set->data_read,
                                              group_value);
    set->data_read = group_value;

    /* interrupt enable */
    group_value = set->int_enable;
    group_value = deposit32(group_value, pin_idx, 1,
                            SHARED_FIELD_EX32(data, GPIO_CONTROL_INT_ENABLE));
    set->int_enable = update_value_control_source(set, set->int_enable,
                                                  group_value);

    /* interrupt sensitivity type 0 */
    group_value = set->int_sens_0;
    group_value = deposit32(group_value, pin_idx, 1,
                            SHARED_FIELD_EX32(data, GPIO_CONTROL_INT_SENS_0));
    set->int_sens_0 = update_value_control_source(set, set->int_sens_0,
                                                  group_value);

    /* interrupt sensitivity type 1 */
    group_value = set->int_sens_1;
    group_value = deposit32(group_value, pin_idx, 1,
                            SHARED_FIELD_EX32(data, GPIO_CONTROL_INT_SENS_1));
    set->int_sens_1 = update_value_control_source(set, set->int_sens_1,
                                                  group_value);

    /* interrupt sensitivity type 2 */
    group_value = set->int_sens_2;
    group_value = deposit32(group_value, pin_idx, 1,
                            SHARED_FIELD_EX32(data, GPIO_CONTROL_INT_SENS_2));
    set->int_sens_2 = update_value_control_source(set, set->int_sens_2,
                                                  group_value);

    /* reset tolerance enable */
    group_value = set->reset_tol;
    group_value = deposit32(group_value, pin_idx, 1,
                        SHARED_FIELD_EX32(data, GPIO_CONTROL_RESET_TOLERANCE));
    set->reset_tol = update_value_control_source(set, set->reset_tol,
                                                 group_value);

    /* debounce 1 */
    group_value = set->debounce_1;
    group_value = deposit32(group_value, pin_idx, 1,
                            SHARED_FIELD_EX32(data, GPIO_CONTROL_DEBOUNCE_1));
    set->debounce_1 = update_value_control_source(set, set->debounce_1,
                                                  group_value);

    /* debounce 2 */
    group_value = set->debounce_2;
    group_value = deposit32(group_value, pin_idx, 1,
                            SHARED_FIELD_EX32(data, GPIO_CONTROL_DEBOUNCE_2));
    set->debounce_2 = update_value_control_source(set, set->debounce_2,
                                                  group_value);

    /* input mask */
    group_value = set->input_mask;
    group_value = deposit32(group_value, pin_idx, 1,
                            SHARED_FIELD_EX32(data, GPIO_CONTROL_INPUT_MASK));
    /*
     * feeds into interrupt generation
     * 0: read from data value reg will be updated
     * 1: read from data value reg will not be updated
     */
    set->input_mask = group_value & props->input;

    /* blink counter 1 */
    /* blink counter 2 */
    /* unimplement */

    /* interrupt status */
    if (SHARED_FIELD_EX32(data, GPIO_CONTROL_INT_STATUS)) {
        /* pending is either 1 or 0 for a 1-bit field */
        pending = extract32(set->int_status, pin_idx, 1);

        assert(s->pending >= pending);

        /* No change to s->pending if pending is 0 */
        s->pending -= pending;

        /*
         * The write acknowledged the interrupt regardless of whether it
         * was pending or not. The post-condition is that it mustn't be
         * pending. Unconditionally clear the status bit.
         */
        set->int_status = deposit32(set->int_status, pin_idx, 1, 0);
    }

    aspeed_gpio_update(s, set, set->data_value, UINT32_MAX);
}

static uint64_t aspeed_gpio_2700_read(void *opaque, hwaddr offset,
                                uint32_t size)
{
    AspeedGPIOState *s = ASPEED_GPIO(opaque);
    AspeedGPIOClass *agc = ASPEED_GPIO_GET_CLASS(s);
    GPIOSets *set;
    uint64_t value;
    uint64_t reg;
    uint32_t pin;
    uint32_t idx;

    reg = offset >> 2;

    if (reg >= agc->reg_table_count) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: offset 0x%" PRIx64 " out of bounds\n",
                      __func__, offset);
        return 0;
    }

    switch (reg) {
    case R_GPIO_2700_DEBOUNCE_TIME_1 ... R_GPIO_2700_DEBOUNCE_TIME_3:
        idx = reg - R_GPIO_2700_DEBOUNCE_TIME_1;

        if (idx >= ASPEED_GPIO_NR_DEBOUNCE_REGS) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: debounce index: %d, out of bounds\n",
                          __func__, idx);
            return 0;
        }

        value = (uint64_t) s->debounce_regs[idx];
        break;
    case R_GPIO_2700_INT_STATUS_1 ... R_GPIO_2700_INT_STATUS_7:
        idx = reg - R_GPIO_2700_INT_STATUS_1;

        if (idx >= agc->nr_gpio_sets) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: interrupt status index: %d, out of bounds\n",
                          __func__, idx);
            return 0;
        }

        set = &s->sets[idx];
        value = (uint64_t) set->int_status;
        break;
    case R_GPIO_A0_CONTROL ... R_GPIO_AA7_CONTROL:
        pin = reg - R_GPIO_A0_CONTROL;

        if (pin >= agc->nr_gpio_pins) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid pin number: %d\n",
                          __func__, pin);
               return 0;
        }

        value = aspeed_gpio_2700_read_control_reg(s, pin);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: no getter for offset 0x%"
                      PRIx64"\n", __func__, offset);
        return 0;
    }

    trace_aspeed_gpio_read(offset, value);
    return value;
}

static void aspeed_gpio_2700_write(void *opaque, hwaddr offset,
                                uint64_t data, uint32_t size)
{
    AspeedGPIOState *s = ASPEED_GPIO(opaque);
    AspeedGPIOClass *agc = ASPEED_GPIO_GET_CLASS(s);
    uint64_t reg;
    uint32_t pin;
    uint32_t idx;

    trace_aspeed_gpio_write(offset, data);

    reg = offset >> 2;

    if (reg >= agc->reg_table_count) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: offset 0x%" PRIx64 " out of bounds\n",
                      __func__, offset);
        return;
    }

    switch (reg) {
    case R_GPIO_2700_DEBOUNCE_TIME_1 ... R_GPIO_2700_DEBOUNCE_TIME_3:
        idx = reg - R_GPIO_2700_DEBOUNCE_TIME_1;

        if (idx >= ASPEED_GPIO_NR_DEBOUNCE_REGS) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: debounce index: %d out of bounds\n",
                          __func__, idx);
            return;
        }

        s->debounce_regs[idx] = (uint32_t) data;
        break;
    case R_GPIO_A0_CONTROL ... R_GPIO_AA7_CONTROL:
        pin = reg - R_GPIO_A0_CONTROL;

        if (pin >= agc->nr_gpio_pins) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid pin number: %d\n",
                          __func__, pin);
            return;
        }

        if (SHARED_FIELD_EX32(data, GPIO_CONTROL_RESERVED)) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid reserved data: 0x%"
                          PRIx64"\n", __func__, data);
            return;
        }

        aspeed_gpio_2700_write_control_reg(s, pin, data);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: no setter for offset 0x%"
                      PRIx64"\n", __func__, offset);
        break;
    }
}

/* Setup functions */
static void aspeed_gpio_set_set(Object *obj, Visitor *v,
                                        const char *name, void *opaque,
                                        Error **errp)
{
    uint32_t set_val = 0;
    AspeedGPIOState *s = ASPEED_GPIO(obj);
    AspeedGPIOClass *agc = ASPEED_GPIO_GET_CLASS(s);
    int set_idx = 0;

    if (!visit_type_uint32(v, name, &set_val, errp)) {
        return;
    }

    if (sscanf(name, "gpio-set[%d]", &set_idx) != 1) {
        error_setg(errp, "%s: error reading %s", __func__, name);
        return;
    }

    if (set_idx >= agc->nr_gpio_sets || set_idx < 0) {
        error_setg(errp, "%s: invalid set_idx %s", __func__, name);
        return;
    }

    aspeed_gpio_update(s, &s->sets[set_idx], set_val,
                       ~s->sets[set_idx].direction);
}

static void aspeed_gpio_get_set(Object *obj, Visitor *v,
                                        const char *name, void *opaque,
                                        Error **errp)
{
    uint32_t set_val = 0;
    AspeedGPIOState *s = ASPEED_GPIO(obj);
    AspeedGPIOClass *agc = ASPEED_GPIO_GET_CLASS(s);
    int set_idx = 0;

    if (sscanf(name, "gpio-set[%d]", &set_idx) != 1) {
        error_setg(errp, "%s: error reading %s", __func__, name);
        return;
    }

    if (set_idx >= agc->nr_gpio_sets || set_idx < 0) {
        error_setg(errp, "%s: invalid set_idx %s", __func__, name);
        return;
    }

    set_val = s->sets[set_idx].data_value;
    visit_type_uint32(v, name, &set_val, errp);
}

/****************** Setup functions ******************/
static const GPIOSetProperties ast2400_set_props[ASPEED_GPIO_MAX_NR_SETS] = {
    [0] = {0xffffffff,  0xffffffff,  {"A", "B", "C", "D"} },
    [1] = {0xffffffff,  0xffffffff,  {"E", "F", "G", "H"} },
    [2] = {0xffffffff,  0xffffffff,  {"I", "J", "K", "L"} },
    [3] = {0xffffffff,  0xffffffff,  {"M", "N", "O", "P"} },
    [4] = {0xffffffff,  0xffffffff,  {"Q", "R", "S", "T"} },
    [5] = {0xffffffff,  0x0000ffff,  {"U", "V", "W", "X"} },
    [6] = {0x0000000f,  0x0fffff0f,  {"Y", "Z", "AA", "AB"} },
};

static const GPIOSetProperties ast2500_set_props[ASPEED_GPIO_MAX_NR_SETS] = {
    [0] = {0xffffffff,  0xffffffff,  {"A", "B", "C", "D"} },
    [1] = {0xffffffff,  0xffffffff,  {"E", "F", "G", "H"} },
    [2] = {0xffffffff,  0xffffffff,  {"I", "J", "K", "L"} },
    [3] = {0xffffffff,  0xffffffff,  {"M", "N", "O", "P"} },
    [4] = {0xffffffff,  0xffffffff,  {"Q", "R", "S", "T"} },
    [5] = {0xffffffff,  0x0000ffff,  {"U", "V", "W", "X"} },
    [6] = {0x0fffffff,  0x0fffffff,  {"Y", "Z", "AA", "AB"} },
    [7] = {0x000000ff,  0x000000ff,  {"AC"} },
};

static GPIOSetProperties ast2600_3_3v_set_props[ASPEED_GPIO_MAX_NR_SETS] = {
    [0] = {0xffffffff,  0xffffffff,  {"A", "B", "C", "D"} },
    [1] = {0xffffffff,  0xffffffff,  {"E", "F", "G", "H"} },
    [2] = {0xffffffff,  0xffffffff,  {"I", "J", "K", "L"} },
    [3] = {0xffffffff,  0xffffffff,  {"M", "N", "O", "P"} },
    [4] = {0xffffffff,  0x00ffffff,  {"Q", "R", "S", "T"} },
    [5] = {0xffffffff,  0xffffff00,  {"U", "V", "W", "X"} },
    [6] = {0x0000ffff,  0x0000ffff,  {"Y", "Z"} },
};

static GPIOSetProperties ast2600_1_8v_set_props[ASPEED_GPIO_MAX_NR_SETS] = {
    [0] = {0xffffffff,  0xffffffff,  {"18A", "18B", "18C", "18D"} },
    [1] = {0x0000000f,  0x0000000f,  {"18E"} },
};

static GPIOSetProperties ast1030_set_props[ASPEED_GPIO_MAX_NR_SETS] = {
    [0] = {0xffffffff,  0xffffffff,  {"A", "B", "C", "D"} },
    [1] = {0xffffffff,  0xffffffff,  {"E", "F", "G", "H"} },
    [2] = {0xffffffff,  0xffffffff,  {"I", "J", "K", "L"} },
    [3] = {0xffffff3f,  0xffffff3f,  {"M", "N", "O", "P"} },
    [4] = {0xff060c1f,  0x00060c1f,  {"Q", "R", "S", "T"} },
    [5] = {0x000000ff,  0x00000000,  {"U"} },
};

static GPIOSetProperties ast2700_set_props[ASPEED_GPIO_MAX_NR_SETS] = {
    [0] = {0xffffffff,  0xffffffff,  {"A", "B", "C", "D"} },
    [1] = {0x0fffffff,  0x0fffffff,  {"E", "F", "G", "H"} },
    [2] = {0xffffffff,  0xffffffff,  {"I", "J", "K", "L"} },
    [3] = {0xffffffff,  0xffffffff,  {"M", "N", "O", "P"} },
    [4] = {0xffffffff,  0xffffffff,  {"Q", "R", "S", "T"} },
    [5] = {0xffffffff,  0xffffffff,  {"U", "V", "W", "X"} },
    [6] = {0x00ffffff,  0x00ffffff,  {"Y", "Z", "AA"} },
};

static const MemoryRegionOps aspeed_gpio_ops = {
    .read       = aspeed_gpio_read,
    .write      = aspeed_gpio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static const MemoryRegionOps aspeed_gpio_2700_ops = {
    .read       = aspeed_gpio_2700_read,
    .write      = aspeed_gpio_2700_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void aspeed_gpio_reset(DeviceState *dev)
{
    AspeedGPIOState *s = ASPEED_GPIO(dev);

    /* TODO: respect the reset tolerance registers */
    memset(s->sets, 0, sizeof(s->sets));
}

static void aspeed_gpio_realize(DeviceState *dev, Error **errp)
{
    AspeedGPIOState *s = ASPEED_GPIO(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    AspeedGPIOClass *agc = ASPEED_GPIO_GET_CLASS(s);

    /* Interrupt parent line */
    sysbus_init_irq(sbd, &s->irq);

    /* Individual GPIOs */
    for (int i = 0; i < ASPEED_GPIO_MAX_NR_SETS; i++) {
        const GPIOSetProperties *props = &agc->props[i];
        uint32_t skip = ~(props->input | props->output);
        for (int j = 0; j < ASPEED_GPIOS_PER_SET; j++) {
            if (skip >> j & 1) {
                continue;
            }
            sysbus_init_irq(sbd, &s->gpios[i][j]);
        }
    }

    memory_region_init_io(&s->iomem, OBJECT(s), agc->reg_ops, s,
                          TYPE_ASPEED_GPIO, agc->mem_size);

    sysbus_init_mmio(sbd, &s->iomem);
}

static void aspeed_gpio_init(Object *obj)
{
    AspeedGPIOState *s = ASPEED_GPIO(obj);
    AspeedGPIOClass *agc = ASPEED_GPIO_GET_CLASS(s);

    for (int i = 0; i < ASPEED_GPIO_MAX_NR_SETS; i++) {
        const GPIOSetProperties *props = &agc->props[i];
        uint32_t skip = ~(props->input | props->output);
        for (int j = 0; j < ASPEED_GPIOS_PER_SET; j++) {
            if (skip >> j & 1) {
                continue;
            }
            int group_idx = j / GPIOS_PER_GROUP;
            int pin_idx = j % GPIOS_PER_GROUP;
            const char *group = &props->group_label[group_idx][0];
            char *name = g_strdup_printf("gpio%s%d", group, pin_idx);
            object_property_add(obj, name, "bool", aspeed_gpio_get_pin,
                                aspeed_gpio_set_pin, NULL, NULL);
            g_free(name);
        }
    }

    for (int i = 0; i < agc->nr_gpio_sets; i++) {
        char *name = g_strdup_printf("gpio-set[%d]", i);
        object_property_add(obj, name, "uint32", aspeed_gpio_get_set,
        aspeed_gpio_set_set, NULL, NULL);
    }
}

static const VMStateDescription vmstate_gpio_regs = {
    .name = TYPE_ASPEED_GPIO"/regs",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(data_value,   GPIOSets),
        VMSTATE_UINT32(data_read,    GPIOSets),
        VMSTATE_UINT32(direction,    GPIOSets),
        VMSTATE_UINT32(int_enable,   GPIOSets),
        VMSTATE_UINT32(int_sens_0,   GPIOSets),
        VMSTATE_UINT32(int_sens_1,   GPIOSets),
        VMSTATE_UINT32(int_sens_2,   GPIOSets),
        VMSTATE_UINT32(int_status,   GPIOSets),
        VMSTATE_UINT32(reset_tol,    GPIOSets),
        VMSTATE_UINT32(cmd_source_0, GPIOSets),
        VMSTATE_UINT32(cmd_source_1, GPIOSets),
        VMSTATE_UINT32(debounce_1,   GPIOSets),
        VMSTATE_UINT32(debounce_2,   GPIOSets),
        VMSTATE_UINT32(input_mask,   GPIOSets),
        VMSTATE_END_OF_LIST(),
    }
};

static const VMStateDescription vmstate_aspeed_gpio = {
    .name = TYPE_ASPEED_GPIO,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT_ARRAY(sets, AspeedGPIOState, ASPEED_GPIO_MAX_NR_SETS,
                             1, vmstate_gpio_regs, GPIOSets),
        VMSTATE_UINT32_ARRAY(debounce_regs, AspeedGPIOState,
                             ASPEED_GPIO_NR_DEBOUNCE_REGS),
        VMSTATE_END_OF_LIST(),
   }
};

static void aspeed_gpio_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = aspeed_gpio_realize;
    device_class_set_legacy_reset(dc, aspeed_gpio_reset);
    dc->desc = "Aspeed GPIO Controller";
    dc->vmsd = &vmstate_aspeed_gpio;
}

static void aspeed_gpio_ast2400_class_init(ObjectClass *klass, const void *data)
{
    AspeedGPIOClass *agc = ASPEED_GPIO_CLASS(klass);

    agc->props = ast2400_set_props;
    agc->nr_gpio_pins = 216;
    agc->nr_gpio_sets = 7;
    agc->reg_table = aspeed_3_3v_gpios;
    agc->reg_table_count = GPIO_3_3V_REG_ARRAY_SIZE;
    agc->mem_size = 0x1000;
    agc->reg_ops = &aspeed_gpio_ops;
}

static void aspeed_gpio_2500_class_init(ObjectClass *klass, const void *data)
{
    AspeedGPIOClass *agc = ASPEED_GPIO_CLASS(klass);

    agc->props = ast2500_set_props;
    agc->nr_gpio_pins = 228;
    agc->nr_gpio_sets = 8;
    agc->reg_table = aspeed_3_3v_gpios;
    agc->reg_table_count = GPIO_3_3V_REG_ARRAY_SIZE;
    agc->mem_size = 0x1000;
    agc->reg_ops = &aspeed_gpio_ops;
}

static void aspeed_gpio_ast2600_3_3v_class_init(ObjectClass *klass,
                                                const void *data)
{
    AspeedGPIOClass *agc = ASPEED_GPIO_CLASS(klass);

    agc->props = ast2600_3_3v_set_props;
    agc->nr_gpio_pins = 208;
    agc->nr_gpio_sets = 7;
    agc->reg_table = aspeed_3_3v_gpios;
    agc->reg_table_count = GPIO_3_3V_REG_ARRAY_SIZE;
    agc->mem_size = 0x800;
    agc->reg_ops = &aspeed_gpio_ops;
}

static void aspeed_gpio_ast2600_1_8v_class_init(ObjectClass *klass,
                                                const void *data)
{
    AspeedGPIOClass *agc = ASPEED_GPIO_CLASS(klass);

    agc->props = ast2600_1_8v_set_props;
    agc->nr_gpio_pins = 36;
    agc->nr_gpio_sets = 2;
    agc->reg_table = aspeed_1_8v_gpios;
    agc->reg_table_count = GPIO_1_8V_REG_ARRAY_SIZE;
    agc->mem_size = 0x800;
    agc->reg_ops = &aspeed_gpio_ops;
}

static void aspeed_gpio_1030_class_init(ObjectClass *klass, const void *data)
{
    AspeedGPIOClass *agc = ASPEED_GPIO_CLASS(klass);

    agc->props = ast1030_set_props;
    agc->nr_gpio_pins = 151;
    agc->nr_gpio_sets = 6;
    agc->reg_table = aspeed_3_3v_gpios;
    agc->reg_table_count = GPIO_3_3V_REG_ARRAY_SIZE;
    agc->mem_size = 0x1000;
    agc->reg_ops = &aspeed_gpio_ops;
}

static void aspeed_gpio_2700_class_init(ObjectClass *klass, const void *data)
{
    AspeedGPIOClass *agc = ASPEED_GPIO_CLASS(klass);

    agc->props = ast2700_set_props;
    agc->nr_gpio_pins = 216;
    agc->nr_gpio_sets = 7;
    agc->reg_table_count = GPIO_2700_REG_ARRAY_SIZE;
    agc->mem_size = 0x1000;
    agc->reg_ops = &aspeed_gpio_2700_ops;
}

static const TypeInfo aspeed_gpio_info = {
    .name           = TYPE_ASPEED_GPIO,
    .parent         = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(AspeedGPIOState),
    .class_size     = sizeof(AspeedGPIOClass),
    .class_init     = aspeed_gpio_class_init,
    .abstract       = true,
};

static const TypeInfo aspeed_gpio_ast2400_info = {
    .name           = TYPE_ASPEED_GPIO "-ast2400",
    .parent         = TYPE_ASPEED_GPIO,
    .class_init     = aspeed_gpio_ast2400_class_init,
    .instance_init  = aspeed_gpio_init,
};

static const TypeInfo aspeed_gpio_ast2500_info = {
    .name           = TYPE_ASPEED_GPIO "-ast2500",
    .parent         = TYPE_ASPEED_GPIO,
    .class_init     = aspeed_gpio_2500_class_init,
    .instance_init  = aspeed_gpio_init,
};

static const TypeInfo aspeed_gpio_ast2600_3_3v_info = {
    .name           = TYPE_ASPEED_GPIO "-ast2600",
    .parent         = TYPE_ASPEED_GPIO,
    .class_init     = aspeed_gpio_ast2600_3_3v_class_init,
    .instance_init  = aspeed_gpio_init,
};

static const TypeInfo aspeed_gpio_ast2600_1_8v_info = {
    .name           = TYPE_ASPEED_GPIO "-ast2600-1_8v",
    .parent         = TYPE_ASPEED_GPIO,
    .class_init     = aspeed_gpio_ast2600_1_8v_class_init,
    .instance_init  = aspeed_gpio_init,
};

static const TypeInfo aspeed_gpio_ast1030_info = {
    .name           = TYPE_ASPEED_GPIO "-ast1030",
    .parent         = TYPE_ASPEED_GPIO,
    .class_init     = aspeed_gpio_1030_class_init,
    .instance_init  = aspeed_gpio_init,
};

static const TypeInfo aspeed_gpio_ast2700_info = {
    .name           = TYPE_ASPEED_GPIO "-ast2700",
    .parent         = TYPE_ASPEED_GPIO,
    .class_init     = aspeed_gpio_2700_class_init,
    .instance_init  = aspeed_gpio_init,
};

static void aspeed_gpio_register_types(void)
{
    type_register_static(&aspeed_gpio_info);
    type_register_static(&aspeed_gpio_ast2400_info);
    type_register_static(&aspeed_gpio_ast2500_info);
    type_register_static(&aspeed_gpio_ast2600_3_3v_info);
    type_register_static(&aspeed_gpio_ast2600_1_8v_info);
    type_register_static(&aspeed_gpio_ast1030_info);
    type_register_static(&aspeed_gpio_ast2700_info);
}

type_init(aspeed_gpio_register_types);
