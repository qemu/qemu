#ifndef HW_ARM_ALLWINNER_A10_H
#define HW_ARM_ALLWINNER_A10_H

#include "hw/char/serial.h"
#include "hw/arm/boot.h"
#include "hw/pci/pci_device.h"
#include "hw/timer/allwinner-a10-pit.h"
#include "hw/intc/allwinner-a10-pic.h"
#include "hw/net/allwinner_emac.h"
#include "hw/sd/allwinner-sdhost.h"
#include "hw/ide/ahci.h"
#include "hw/usb/hcd-ohci.h"
#include "hw/usb/hcd-ehci.h"
#include "hw/rtc/allwinner-rtc.h"
#include "hw/misc/allwinner-a10-ccm.h"
#include "hw/misc/allwinner-a10-dramc.h"
#include "hw/i2c/allwinner-i2c.h"
#include "sysemu/block-backend.h"

#include "target/arm/cpu.h"
#include "qom/object.h"


#define AW_A10_SDRAM_BASE       0x40000000

#define AW_A10_NUM_USB          2

#define TYPE_AW_A10 "allwinner-a10"
OBJECT_DECLARE_SIMPLE_TYPE(AwA10State, AW_A10)

struct AwA10State {
    /*< private >*/
    DeviceState parent_obj;
    /*< public >*/

    ARMCPU cpu;
    AwA10ClockCtlState ccm;
    AwA10DramControllerState dramc;
    AwA10PITState timer;
    AwA10PICState intc;
    AwEmacState emac;
    AllwinnerAHCIState sata;
    AwSdHostState mmc0;
    AWI2CState i2c0;
    AwRtcState rtc;
    MemoryRegion sram_a;
    EHCISysBusState ehci[AW_A10_NUM_USB];
    OHCISysBusState ohci[AW_A10_NUM_USB];
};

/**
 * Emulate Boot ROM firmware setup functionality.
 *
 * A real Allwinner A10 SoC contains a Boot ROM
 * which is the first code that runs right after
 * the SoC is powered on. The Boot ROM is responsible
 * for loading user code (e.g. a bootloader) from any
 * of the supported external devices and writing the
 * downloaded code to internal SRAM. After loading the SoC
 * begins executing the code written to SRAM.
 *
 * This function emulates the Boot ROM by copying 32 KiB
 * of data at offset 8 KiB from the given block device and writes it to
 * the start of the first internal SRAM memory.
 *
 * @s: Allwinner A10 state object pointer
 * @blk: Block backend device object pointer
 */
void allwinner_a10_bootrom_setup(AwA10State *s, BlockBackend *blk);

#endif
