/*
 *  ASPEED GPIO Controller
 *
 *  Copyright (C) 2017-2018 IBM Corp.
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#ifndef ASPEED_GPIO_H
#define ASPEED_GPIO_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_ASPEED_GPIO "aspeed.gpio"
OBJECT_DECLARE_TYPE(AspeedGPIOState, AspeedGPIOClass, ASPEED_GPIO)

#define ASPEED_GPIO_MAX_NR_SETS 8
#define ASPEED_GPIOS_PER_SET 32
#define ASPEED_REGS_PER_BANK 14
#define ASPEED_GPIO_MAX_NR_REGS (ASPEED_REGS_PER_BANK * ASPEED_GPIO_MAX_NR_SETS)
#define ASPEED_GROUPS_PER_SET 4
#define ASPEED_GPIO_NR_DEBOUNCE_REGS 3
#define ASPEED_CHARS_PER_GROUP_LABEL 4

typedef struct GPIOSets GPIOSets;

typedef struct GPIOSetProperties {
    uint32_t input;
    uint32_t output;
    char group_label[ASPEED_GROUPS_PER_SET][ASPEED_CHARS_PER_GROUP_LABEL];
} GPIOSetProperties;

enum GPIORegType {
    gpio_not_a_reg,
    gpio_reg_data_value,
    gpio_reg_direction,
    gpio_reg_int_enable,
    gpio_reg_int_sens_0,
    gpio_reg_int_sens_1,
    gpio_reg_int_sens_2,
    gpio_reg_int_status,
    gpio_reg_reset_tolerant,
    gpio_reg_debounce_1,
    gpio_reg_debounce_2,
    gpio_reg_cmd_source_0,
    gpio_reg_cmd_source_1,
    gpio_reg_data_read,
    gpio_reg_input_mask,
};

typedef struct AspeedGPIOReg {
    uint16_t set_idx;
    enum GPIORegType type;
 } AspeedGPIOReg;

struct AspeedGPIOClass {
    SysBusDevice parent_obj;
    const GPIOSetProperties *props;
    uint32_t nr_gpio_pins;
    uint32_t nr_gpio_sets;
    const AspeedGPIOReg *reg_table;
};

struct AspeedGPIOState {
    /* <private> */
    SysBusDevice parent;

    /*< public >*/
    MemoryRegion iomem;
    int pending;
    qemu_irq irq;
    qemu_irq gpios[ASPEED_GPIO_MAX_NR_SETS][ASPEED_GPIOS_PER_SET];

/* Parallel GPIO Registers */
    uint32_t debounce_regs[ASPEED_GPIO_NR_DEBOUNCE_REGS];
    struct GPIOSets {
        uint32_t data_value; /* Reflects pin values */
        uint32_t data_read; /* Contains last value written to data value */
        uint32_t direction;
        uint32_t int_enable;
        uint32_t int_sens_0;
        uint32_t int_sens_1;
        uint32_t int_sens_2;
        uint32_t int_status;
        uint32_t reset_tol;
        uint32_t cmd_source_0;
        uint32_t cmd_source_1;
        uint32_t debounce_1;
        uint32_t debounce_2;
        uint32_t input_mask;
    } sets[ASPEED_GPIO_MAX_NR_SETS];
};

#endif /* _ASPEED_GPIO_H_ */
