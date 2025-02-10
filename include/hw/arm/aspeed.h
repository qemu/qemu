/*
 * Aspeed Machines
 *
 * Copyright 2018 IBM Corp.
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */
#ifndef ARM_ASPEED_H
#define ARM_ASPEED_H

#include "hw/boards.h"
#include "qom/object.h"

typedef struct AspeedMachineState AspeedMachineState;

#define TYPE_ASPEED_MACHINE       MACHINE_TYPE_NAME("aspeed")
typedef struct AspeedMachineClass AspeedMachineClass;
DECLARE_OBJ_CHECKERS(AspeedMachineState, AspeedMachineClass,
                     ASPEED_MACHINE, TYPE_ASPEED_MACHINE)

#define ASPEED_MAC0_ON   (1 << 0)
#define ASPEED_MAC1_ON   (1 << 1)
#define ASPEED_MAC2_ON   (1 << 2)
#define ASPEED_MAC3_ON   (1 << 3)


struct AspeedMachineClass {
    MachineClass parent_obj;

    const char *name;
    const char *desc;
    const char *soc_name;
    uint32_t hw_strap1;
    uint32_t hw_strap2;
    const char *fmc_model;
    const char *spi_model;
    uint32_t num_cs;
    uint32_t macs_mask;
    void (*i2c_init)(AspeedMachineState *bmc);
    uint32_t uart_default;
    bool sdhci_wp_inverted;
};


#endif
