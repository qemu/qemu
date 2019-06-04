#ifndef HW_ARM_ALLWINNER_A10_H
#define HW_ARM_ALLWINNER_A10_H

#include "qemu/error-report.h"
#include "hw/char/serial.h"
#include "hw/arm/boot.h"
#include "hw/timer/allwinner-a10-pit.h"
#include "hw/intc/allwinner-a10-pic.h"
#include "hw/net/allwinner_emac.h"
#include "hw/ide/pci.h"
#include "hw/ide/ahci.h"

#include "sysemu/sysemu.h"


#define AW_A10_PIC_REG_BASE     0x01c20400
#define AW_A10_PIT_REG_BASE     0x01c20c00
#define AW_A10_UART0_REG_BASE   0x01c28000
#define AW_A10_EMAC_BASE        0x01c0b000
#define AW_A10_SATA_BASE        0x01c18000

#define AW_A10_SDRAM_BASE       0x40000000

#define TYPE_AW_A10 "allwinner-a10"
#define AW_A10(obj) OBJECT_CHECK(AwA10State, (obj), TYPE_AW_A10)

typedef struct AwA10State {
    /*< private >*/
    DeviceState parent_obj;
    /*< public >*/

    ARMCPU cpu;
    qemu_irq irq[AW_A10_PIC_INT_NR];
    AwA10PITState timer;
    AwA10PICState intc;
    AwEmacState emac;
    AllwinnerAHCIState sata;
    MemoryRegion sram_a;
} AwA10State;

#endif
