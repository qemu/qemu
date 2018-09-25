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

typedef struct AspeedBoardState AspeedBoardState;

typedef struct AspeedBoardConfig {
    const char *name;
    const char *desc;
    const char *soc_name;
    uint32_t hw_strap1;
    const char *fmc_model;
    const char *spi_model;
    uint32_t num_cs;
    void (*i2c_init)(AspeedBoardState *bmc);
} AspeedBoardConfig;

#define TYPE_ASPEED_MACHINE       MACHINE_TYPE_NAME("aspeed")
#define ASPEED_MACHINE(obj) \
    OBJECT_CHECK(AspeedMachine, (obj), TYPE_ASPEED_MACHINE)

typedef struct AspeedMachine {
    MachineState parent_obj;
} AspeedMachine;

#define ASPEED_MACHINE_CLASS(klass) \
     OBJECT_CLASS_CHECK(AspeedMachineClass, (klass), TYPE_ASPEED_MACHINE)
#define ASPEED_MACHINE_GET_CLASS(obj) \
     OBJECT_GET_CLASS(AspeedMachineClass, (obj), TYPE_ASPEED_MACHINE)

typedef struct AspeedMachineClass {
    MachineClass parent_obj;
    const AspeedBoardConfig *board;
} AspeedMachineClass;


#endif
