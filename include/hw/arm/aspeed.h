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
#include "hw/arm/aspeed_soc.h"

typedef struct AspeedMachineState AspeedMachineState;

#define TYPE_ASPEED_MACHINE       MACHINE_TYPE_NAME("aspeed")
typedef struct AspeedMachineClass AspeedMachineClass;
DECLARE_OBJ_CHECKERS(AspeedMachineState, AspeedMachineClass,
                     ASPEED_MACHINE, TYPE_ASPEED_MACHINE)

#define ASPEED_MAC0_ON   (1 << 0)
#define ASPEED_MAC1_ON   (1 << 1)
#define ASPEED_MAC2_ON   (1 << 2)
#define ASPEED_MAC3_ON   (1 << 3)

struct AspeedMachineState {
    MachineState parent_obj;

    AspeedSoCState *soc;
    MemoryRegion boot_rom;
    bool mmio_exec;
    uint32_t uart_chosen;
    char *fmc_model;
    char *spi_model;
    uint32_t hw_strap1;
};

struct AspeedMachineClass {
    MachineClass parent_obj;

    const char *name;
    const char *desc;
    const char *soc_name;
    uint32_t hw_strap1;
    uint32_t hw_strap2;
    const char *fmc_model;
    const char *spi_model;
    const char *spi2_model;
    uint32_t num_cs;
    uint32_t num_cs2;
    uint32_t macs_mask;
    void (*i2c_init)(AspeedMachineState *bmc);
    uint32_t uart_default;
    bool sdhci_wp_inverted;
    bool vbootrom;
};

/*
 * aspeed_machine_class_init_cpus_defaults:
 * @mc: the #MachineClass to be initialized.
 *
 * Initialize the default CPU configuration for an Aspeed machine class.
 * This function sets the default, minimum, and maximum CPU counts
 * to match the number of CPUs defined in the associated SoC class,
 * and copies its list of valid CPU types.
 */
void aspeed_machine_class_init_cpus_defaults(MachineClass *mc);

/*
 * aspeed_create_pca9552:
 * @soc: pointer to the #AspeedSoCState.
 * @bus_id: the I2C bus index to attach the device.
 * @addr: the I2C address of the PCA9552 device.
 *
 * Create and attach a PCA9552 LED controller device to the specified I2C bus
 * of the given Aspeed SoC. The device is instantiated using
 * i2c_slave_create_simple() with the PCA9552 device type.
 */
void aspeed_create_pca9552(AspeedSoCState *soc, int bus_id, int addr);

#endif
