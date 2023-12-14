/*
 * ASPEED SoC family
 *
 * Andrew Jeffery <andrew@aj.id.au>
 *
 * Copyright 2016 IBM Corp.
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#ifndef ASPEED_SOC_H
#define ASPEED_SOC_H

#include "hw/cpu/a15mpcore.h"
#include "hw/arm/armv7m.h"
#include "hw/intc/aspeed_vic.h"
#include "hw/misc/aspeed_scu.h"
#include "hw/adc/aspeed_adc.h"
#include "hw/misc/aspeed_sdmc.h"
#include "hw/misc/aspeed_xdma.h"
#include "hw/timer/aspeed_timer.h"
#include "hw/rtc/aspeed_rtc.h"
#include "hw/i2c/aspeed_i2c.h"
#include "hw/misc/aspeed_i3c.h"
#include "hw/ssi/aspeed_smc.h"
#include "hw/misc/aspeed_hace.h"
#include "hw/misc/aspeed_sbc.h"
#include "hw/watchdog/wdt_aspeed.h"
#include "hw/net/ftgmac100.h"
#include "target/arm/cpu.h"
#include "hw/gpio/aspeed_gpio.h"
#include "hw/sd/aspeed_sdhci.h"
#include "hw/usb/hcd-ehci.h"
#include "qom/object.h"
#include "hw/misc/aspeed_lpc.h"
#include "hw/misc/unimp.h"
#include "hw/misc/aspeed_peci.h"
#include "hw/char/serial.h"

#define ASPEED_SPIS_NUM  2
#define ASPEED_EHCIS_NUM 2
#define ASPEED_WDTS_NUM  4
#define ASPEED_CPUS_NUM  2
#define ASPEED_MACS_NUM  4
#define ASPEED_UARTS_NUM 13
#define ASPEED_JTAG_NUM  2

struct AspeedSoCState {
    DeviceState parent;

    MemoryRegion *memory;
    MemoryRegion *dram_mr;
    MemoryRegion dram_container;
    MemoryRegion sram;
    MemoryRegion spi_boot_container;
    MemoryRegion spi_boot;
    AspeedRtcState rtc;
    AspeedTimerCtrlState timerctrl;
    AspeedI2CState i2c;
    AspeedI3CState i3c;
    AspeedSCUState scu;
    AspeedHACEState hace;
    AspeedXDMAState xdma;
    AspeedADCState adc;
    AspeedSMCState fmc;
    AspeedSMCState spi[ASPEED_SPIS_NUM];
    EHCISysBusState ehci[ASPEED_EHCIS_NUM];
    AspeedSBCState sbc;
    MemoryRegion secsram;
    UnimplementedDeviceState sbc_unimplemented;
    AspeedSDMCState sdmc;
    AspeedWDTState wdt[ASPEED_WDTS_NUM];
    FTGMAC100State ftgmac100[ASPEED_MACS_NUM];
    AspeedMiiState mii[ASPEED_MACS_NUM];
    AspeedGPIOState gpio;
    AspeedGPIOState gpio_1_8v;
    AspeedSDHCIState sdhci;
    AspeedSDHCIState emmc;
    AspeedLPCState lpc;
    AspeedPECIState peci;
    SerialMM uart[ASPEED_UARTS_NUM];
    Clock *sysclk;
    UnimplementedDeviceState iomem;
    UnimplementedDeviceState video;
    UnimplementedDeviceState emmc_boot_controller;
    UnimplementedDeviceState dpmcu;
    UnimplementedDeviceState pwm;
    UnimplementedDeviceState espi;
    UnimplementedDeviceState udc;
    UnimplementedDeviceState sgpiom;
    UnimplementedDeviceState jtag[ASPEED_JTAG_NUM];
};

#define TYPE_ASPEED_SOC "aspeed-soc"
OBJECT_DECLARE_TYPE(AspeedSoCState, AspeedSoCClass, ASPEED_SOC)

struct Aspeed2400SoCState {
    AspeedSoCState parent;

    ARMCPU cpu[ASPEED_CPUS_NUM];
    AspeedVICState vic;
};

#define TYPE_ASPEED2400_SOC "aspeed2400-soc"
OBJECT_DECLARE_SIMPLE_TYPE(Aspeed2400SoCState, ASPEED2400_SOC)

struct Aspeed2600SoCState {
    AspeedSoCState parent;

    A15MPPrivState a7mpcore;
    ARMCPU cpu[ASPEED_CPUS_NUM]; /* XXX belong to a7mpcore */
};

#define TYPE_ASPEED2600_SOC "aspeed2600-soc"
OBJECT_DECLARE_SIMPLE_TYPE(Aspeed2600SoCState, ASPEED2600_SOC)

struct Aspeed10x0SoCState {
    AspeedSoCState parent;

    ARMv7MState armv7m;
};

#define TYPE_ASPEED10X0_SOC "aspeed10x0-soc"
OBJECT_DECLARE_SIMPLE_TYPE(Aspeed10x0SoCState, ASPEED10X0_SOC)

struct AspeedSoCClass {
    DeviceClass parent_class;

    const char *name;
    const char *cpu_type;
    uint32_t silicon_rev;
    uint64_t sram_size;
    uint64_t secsram_size;
    int spis_num;
    int ehcis_num;
    int wdts_num;
    int macs_num;
    int uarts_num;
    const int *irqmap;
    const hwaddr *memmap;
    uint32_t num_cpus;
    qemu_irq (*get_irq)(AspeedSoCState *s, int dev);
};


enum {
    ASPEED_DEV_SPI_BOOT,
    ASPEED_DEV_IOMEM,
    ASPEED_DEV_UART1,
    ASPEED_DEV_UART2,
    ASPEED_DEV_UART3,
    ASPEED_DEV_UART4,
    ASPEED_DEV_UART5,
    ASPEED_DEV_UART6,
    ASPEED_DEV_UART7,
    ASPEED_DEV_UART8,
    ASPEED_DEV_UART9,
    ASPEED_DEV_UART10,
    ASPEED_DEV_UART11,
    ASPEED_DEV_UART12,
    ASPEED_DEV_UART13,
    ASPEED_DEV_VUART,
    ASPEED_DEV_FMC,
    ASPEED_DEV_SPI1,
    ASPEED_DEV_SPI2,
    ASPEED_DEV_EHCI1,
    ASPEED_DEV_EHCI2,
    ASPEED_DEV_VIC,
    ASPEED_DEV_SDMC,
    ASPEED_DEV_SCU,
    ASPEED_DEV_ADC,
    ASPEED_DEV_SBC,
    ASPEED_DEV_SECSRAM,
    ASPEED_DEV_EMMC_BC,
    ASPEED_DEV_VIDEO,
    ASPEED_DEV_SRAM,
    ASPEED_DEV_SDHCI,
    ASPEED_DEV_GPIO,
    ASPEED_DEV_GPIO_1_8V,
    ASPEED_DEV_RTC,
    ASPEED_DEV_TIMER1,
    ASPEED_DEV_TIMER2,
    ASPEED_DEV_TIMER3,
    ASPEED_DEV_TIMER4,
    ASPEED_DEV_TIMER5,
    ASPEED_DEV_TIMER6,
    ASPEED_DEV_TIMER7,
    ASPEED_DEV_TIMER8,
    ASPEED_DEV_WDT,
    ASPEED_DEV_PWM,
    ASPEED_DEV_LPC,
    ASPEED_DEV_IBT,
    ASPEED_DEV_I2C,
    ASPEED_DEV_PECI,
    ASPEED_DEV_ETH1,
    ASPEED_DEV_ETH2,
    ASPEED_DEV_ETH3,
    ASPEED_DEV_ETH4,
    ASPEED_DEV_MII1,
    ASPEED_DEV_MII2,
    ASPEED_DEV_MII3,
    ASPEED_DEV_MII4,
    ASPEED_DEV_SDRAM,
    ASPEED_DEV_XDMA,
    ASPEED_DEV_EMMC,
    ASPEED_DEV_KCS,
    ASPEED_DEV_HACE,
    ASPEED_DEV_DPMCU,
    ASPEED_DEV_DP,
    ASPEED_DEV_I3C,
    ASPEED_DEV_ESPI,
    ASPEED_DEV_UDC,
    ASPEED_DEV_SGPIOM,
    ASPEED_DEV_JTAG0,
    ASPEED_DEV_JTAG1,
};

#define ASPEED_SOC_SPI_BOOT_ADDR 0x0

qemu_irq aspeed_soc_get_irq(AspeedSoCState *s, int dev);
bool aspeed_soc_uart_realize(AspeedSoCState *s, Error **errp);
void aspeed_soc_uart_set_chr(AspeedSoCState *s, int dev, Chardev *chr);
bool aspeed_soc_dram_init(AspeedSoCState *s, Error **errp);
void aspeed_mmio_map(AspeedSoCState *s, SysBusDevice *dev, int n, hwaddr addr);
void aspeed_mmio_map_unimplemented(AspeedSoCState *s, SysBusDevice *dev,
                                   const char *name, hwaddr addr,
                                   uint64_t size);
void aspeed_board_init_flashes(AspeedSMCState *s, const char *flashtype,
                               unsigned int count, int unit0);

#endif /* ASPEED_SOC_H */
