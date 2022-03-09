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
                               uint32_t value)
{
    uint32_t input_mask = regs->input_mask;
    uint32_t direction = regs->direction;
    uint32_t old = regs->data_value;
    uint32_t new = value;
    uint32_t diff;
    int gpio;

    diff = old ^ new;
    if (diff) {
        for (gpio = 0; gpio < ASPEED_GPIOS_PER_SET; gpio++) {
            uint32_t mask = 1 << gpio;

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
        value &= !pin_mask;
    }

    aspeed_gpio_update(s, &s->sets[set_idx], value);
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

    idx = offset >> 2;
    if (idx >= GPIO_DEBOUNCE_TIME_1 && idx <= GPIO_DEBOUNCE_TIME_3) {
        idx -= GPIO_DEBOUNCE_TIME_1;
        return (uint64_t) s->debounce_regs[idx];
    }

    reg = &agc->reg_table[idx];
    if (reg->set_idx >= agc->nr_gpio_sets) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: no getter for offset 0x%"
                      HWADDR_PRIx"\n", __func__, offset);
        return 0;
    }

    set = &s->sets[reg->set_idx];
    switch (reg->type) {
    case gpio_reg_data_value:
        return set->data_value;
    case gpio_reg_direction:
        return set->direction;
    case gpio_reg_int_enable:
        return set->int_enable;
    case gpio_reg_int_sens_0:
        return set->int_sens_0;
    case gpio_reg_int_sens_1:
        return set->int_sens_1;
    case gpio_reg_int_sens_2:
        return set->int_sens_2;
    case gpio_reg_int_status:
        return set->int_status;
    case gpio_reg_reset_tolerant:
        return set->reset_tol;
    case gpio_reg_debounce_1:
        return set->debounce_1;
    case gpio_reg_debounce_2:
        return set->debounce_2;
    case gpio_reg_cmd_source_0:
        return set->cmd_source_0;
    case gpio_reg_cmd_source_1:
        return set->cmd_source_1;
    case gpio_reg_data_read:
        return set->data_read;
    case gpio_reg_input_mask:
        return set->input_mask;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: no getter for offset 0x%"
                      HWADDR_PRIx"\n", __func__, offset);
        return 0;
    }
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

    idx = offset >> 2;
    if (idx >= GPIO_DEBOUNCE_TIME_1 && idx <= GPIO_DEBOUNCE_TIME_3) {
        idx -= GPIO_DEBOUNCE_TIME_1;
        s->debounce_regs[idx] = (uint32_t) data;
        return;
    }

    reg = &agc->reg_table[idx];
    if (reg->set_idx >= agc->nr_gpio_sets) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: no setter for offset 0x%"
                      HWADDR_PRIx"\n", __func__, offset);
        return;
    }

    set = &s->sets[reg->set_idx];
    props = &agc->props[reg->set_idx];

    switch (reg->type) {
    case gpio_reg_data_value:
        data &= props->output;
        data = update_value_control_source(set, set->data_value, data);
        set->data_read = data;
        aspeed_gpio_update(s, set, data);
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
                      HWADDR_PRIx"\n", __func__, offset);
        return;
    }
    aspeed_gpio_update(s, set, set->data_value);
    return;
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

static const MemoryRegionOps aspeed_gpio_ops = {
    .read       = aspeed_gpio_read,
    .write      = aspeed_gpio_write,
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

    memory_region_init_io(&s->iomem, OBJECT(s), &aspeed_gpio_ops, s,
            TYPE_ASPEED_GPIO, 0x800);

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
}

static const VMStateDescription vmstate_gpio_regs = {
    .name = TYPE_ASPEED_GPIO"/regs",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
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
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT_ARRAY(sets, AspeedGPIOState, ASPEED_GPIO_MAX_NR_SETS,
                             1, vmstate_gpio_regs, GPIOSets),
        VMSTATE_UINT32_ARRAY(debounce_regs, AspeedGPIOState,
                             ASPEED_GPIO_NR_DEBOUNCE_REGS),
        VMSTATE_END_OF_LIST(),
   }
};

static void aspeed_gpio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = aspeed_gpio_realize;
    dc->reset = aspeed_gpio_reset;
    dc->desc = "Aspeed GPIO Controller";
    dc->vmsd = &vmstate_aspeed_gpio;
}

static void aspeed_gpio_ast2400_class_init(ObjectClass *klass, void *data)
{
    AspeedGPIOClass *agc = ASPEED_GPIO_CLASS(klass);

    agc->props = ast2400_set_props;
    agc->nr_gpio_pins = 216;
    agc->nr_gpio_sets = 7;
    agc->reg_table = aspeed_3_3v_gpios;
}

static void aspeed_gpio_2500_class_init(ObjectClass *klass, void *data)
{
    AspeedGPIOClass *agc = ASPEED_GPIO_CLASS(klass);

    agc->props = ast2500_set_props;
    agc->nr_gpio_pins = 228;
    agc->nr_gpio_sets = 8;
    agc->reg_table = aspeed_3_3v_gpios;
}

static void aspeed_gpio_ast2600_3_3v_class_init(ObjectClass *klass, void *data)
{
    AspeedGPIOClass *agc = ASPEED_GPIO_CLASS(klass);

    agc->props = ast2600_3_3v_set_props;
    agc->nr_gpio_pins = 208;
    agc->nr_gpio_sets = 7;
    agc->reg_table = aspeed_3_3v_gpios;
}

static void aspeed_gpio_ast2600_1_8v_class_init(ObjectClass *klass, void *data)
{
    AspeedGPIOClass *agc = ASPEED_GPIO_CLASS(klass);

    agc->props = ast2600_1_8v_set_props;
    agc->nr_gpio_pins = 36;
    agc->nr_gpio_sets = 2;
    agc->reg_table = aspeed_1_8v_gpios;
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

static void aspeed_gpio_register_types(void)
{
    type_register_static(&aspeed_gpio_info);
    type_register_static(&aspeed_gpio_ast2400_info);
    type_register_static(&aspeed_gpio_ast2500_info);
    type_register_static(&aspeed_gpio_ast2600_3_3v_info);
    type_register_static(&aspeed_gpio_ast2600_1_8v_info);
}

type_init(aspeed_gpio_register_types);
