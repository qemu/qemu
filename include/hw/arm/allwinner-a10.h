#ifndef HW_ARM_ALLWINNER_A10_H
#define HW_ARM_ALLWINNER_A10_H

#include "qemu/error-report.h"
#include "hw/char/serial.h"
#include "hw/arm/boot.h"
#include "hw/timer/allwinner-a10-pit.h"
#include "hw/intc/allwinner-a10-pic.h"
#include "hw/net/allwinner_emac.h"
#include "hw/sd/allwinner-sdhost.h"
#include "hw/ide/ahci.h"
#include "hw/usb/hcd-ohci.h"
#include "hw/usb/hcd-ehci.h"
#include "hw/rtc/allwinner-rtc.h"

#include "target/arm/cpu.h"


#define AW_A10_SDRAM_BASE       0x40000000

#define AW_A10_NUM_USB          2

#define TYPE_AW_A10 "allwinner-a10"
#define AW_A10(obj) OBJECT_CHECK(AwA10State, (obj), TYPE_AW_A10)

typedef struct AwA10State {
    /*< private >*/
    DeviceState parent_obj;
    /*< public >*/

    ARMCPU cpu;
    AwA10PITState timer;
    AwA10PICState intc;
    AwEmacState emac;
    AllwinnerAHCIState sata;
    AwSdHostState mmc0;
    AwRtcState rtc;
    MemoryRegion sram_a;
    EHCISysBusState ehci[AW_A10_NUM_USB];
    OHCISysBusState ohci[AW_A10_NUM_USB];
} AwA10State;

#endif
