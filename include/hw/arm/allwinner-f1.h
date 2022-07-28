#ifndef HW_ARM_ALLWINNER_F1_H
#define HW_ARM_ALLWINNER_F1_H

#include "qemu/error-report.h"
#include "hw/char/serial.h"
#include "hw/arm/boot.h"
#include "hw/misc/allwinner-cpucfg.h"
#include "hw/sd/allwinner-sdhost.h"
#include "hw/misc/allwinner-ccu.h"
#include "hw/intc/allwinner-f1-pic.h"
#include "hw/timer/allwinner-f1-pit.h"
#include "hw/gpio/allwinner-f1-pio.h"
#include "hw/adc/allwinner-keyadc.h"
#include "hw/display/allwinner-f1-display.h"

#include "target/arm/cpu.h"
#include "qom/object.h"

#define TYPE_AW_F1 "allwinner-f1"

OBJECT_DECLARE_SIMPLE_TYPE(AwF1State, AW_F1)

#define AW_F1_SRAM_ADDR        0x00000000
#define AW_F1_SYSCTRL_REGS     0x01c00000
#define AW_F1_DRAMC_REGS       0x01c01000
#define AW_F1_SPI0_REGS        0x01c05000
#define AW_F1_TCON_REGS        0x01c0c000
#define AW_F1_MMC0_REGS        0x01c0f000
#define AW_F1_CCU_REGS         0x01c20000
#define AW_F1_PIC_REGS         0x01c20400
#define AW_F1_PIO_REGS         0x01c20800
#define AW_F1_PIT_REGS         0x01c20c00
#define AW_F1_KEYADC_REGS      0x01c23400
#define AW_F1_UART0_REGS       0x01c25000
#define AW_F1_DEBE_REGS        0x01e60000
#define AW_F1_SDRAM_ADDR       0x80000000
#define AW_F1_BROM_ADDR        0xffff0000

#define AW_F1_BROM_SIZE        0x00008000

struct AwF1State {
    /*< private >*/
    DeviceState       parent_obj;
    /*< public >*/
    ARMCPU            cpu;
    AwCpuCfgState     cpucfg;
    MemoryRegion      sram;
    MemoryRegion      sysctl;
    //AwF1SysCtrlState  sysctl;
    //AwF1DramCtlState  dramc;
    AwSdHostState     mmc0;
    AwClockCtlState   ccu;
    AwF1PICState      intc;
    AwPIOState        pio;
    AwF1PITState      timer;
    AwKeyAdcState     keyadc;
    AwF1DEBEState     debe;
};

/**
 * Emulate Boot ROM firmware setup functionality.
 *
 * A real Allwinner F1100s SoC contains a Boot ROM
 * which is the first code that runs right after
 * the SoC is powered on. The Boot ROM is responsible
 * for loading user code (e.g. a bootloader) from any
 * of the supported external devices and writing the
 * downloaded code to internal SRAM. After loading the SoC
 * begins executing the code written to SRAM.
 *
 * This function emulates the Boot ROM by copying 32 KiB
 * of data from the given block device and writes it to
 * the start of the first internal SRAM memory.
 *
 * @obj: Allwinner F1100s state object pointer
 * @blk: Block backend device object pointer
 */
void aw_f1_bootrom_setup(Object *obj);
void aw_f1_spl_setup(Object *obj, BlockBackend *blk);
#endif
