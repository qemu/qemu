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

/* On 32-bit hosts, lower RAM to 1G because of the 2047 MB limit */
#if HOST_LONG_BITS == 32
#define ASPEED_RAM_SIZE(sz) MIN((sz), 1 * GiB)
#else
#define ASPEED_RAM_SIZE(sz) (sz)
#endif

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

/*
 * aspeed_create_pca9554:
 * @soc: pointer to the #AspeedSoCState.
 * @bus_id: the I2C bus index to attach the device.
 * @addr: the I2C address of the PCA9554 device.
 *
 * Create and attach a PCA9554 I/O expander to the specified I2C bus
 * of the given Aspeed SoC. The device is created via
 * i2c_slave_create_simple() and returned as an #I2CSlave pointer.
 *
 * Returns: a pointer to the newly created #I2CSlave instance.
 */
I2CSlave *aspeed_create_pca9554(AspeedSoCState *soc, int bus_id, int addr);

/*
 * aspeed_machine_ast2600_class_emmc_init:
 * @oc: the #ObjectClass to initialize.
 *
 * Initialize eMMC-related properties for the AST2600 Aspeed machine class.
 * This function is typically invoked during class initialization to set up
 * default configuration or attach eMMC-specific devices for AST2600 platforms.
 */
void aspeed_machine_ast2600_class_emmc_init(ObjectClass *oc);

/*
 * aspeed_connect_serial_hds_to_uarts:
 * @bmc: pointer to the #AspeedMachineState.
 *
 * Connect host serial backends (character devices) to the UART interfaces
 * of the Aspeed SoC used by the given BMC machine.
 *
 * The function assigns `serial_hd(0)` to the primary UART channel
 * (either chosen via `bmc->uart_chosen` or the machine class default),
 * and iteratively connects remaining serial ports to other available UARTs
 * on the SoC based on their index.
 */
void aspeed_connect_serial_hds_to_uarts(AspeedMachineState *bmc);

#endif
