#ifndef HW_ARM_IPOD_TOUCH_H
#define HW_ARM_IPOD_TOUCH_H

#include "exec/hwaddr.h"
#include "hw/boards.h"
#include "hw/intc/pl192.h"
#include "hw/arm/boot.h"
#include "cpu.h"
#include "hw/dma/pl080.h"
#include "hw/arm/ipod_touch_timer.h"
#include "hw/arm/ipod_touch_clock.h"
#include "hw/arm/ipod_touch_chipid.h"
#include "hw/arm/ipod_touch_gpio.h"
#include "hw/arm/ipod_touch_sysic.h"
#include "hw/arm/ipod_touch_usb_otg.h"
#include "hw/arm/ipod_touch_usb_phys.h"
#include "hw/arm/ipod_touch_spi.h"
#include "hw/arm/ipod_touch_sha1.h"
#include "hw/arm/ipod_touch_aes.h"
#include "hw/arm/ipod_touch_pke.h"

#define TYPE_IPOD_TOUCH "iPod-Touch"

#define S5L8720_VIC_N	  2
#define S5L8720_VIC_SIZE  32

#define S5L8720_TIMER1_IRQ 0x8
#define S5L8720_USB_OTG_IRQ 0x13
#define S5L8720_SPI0_IRQ 0x9
#define S5L8720_SPI1_IRQ 0xA
#define S5L8720_SPI2_IRQ 0xB
#define S5L8720_DMAC0_IRQ 0x10
#define S5L8720_DMAC1_IRQ 0x11
#define S5L8720_SPI3_IRQ 0x1C
#define S5L8720_SPI4_IRQ 0x37

#define IT2G_CPREG_VAR_NAME(name) cpreg_##name
#define IT2G_CPREG_VAR_DEF(name) uint64_t IT2G_CPREG_VAR_NAME(name)

#define TYPE_IPOD_TOUCH_MACHINE   MACHINE_TYPE_NAME(TYPE_IPOD_TOUCH)
#define IPOD_TOUCH_MACHINE(obj) \
    OBJECT_CHECK(IPodTouchMachineState, (obj), TYPE_IPOD_TOUCH_MACHINE)

// memory addresses
#define VROM_MEM_BASE   0x0
#define SRAM1_MEM_BASE  0x22020000
#define SHA1_MEM_BASE 0x38000000
#define DMAC0_MEM_BASE 0x38200000
#define USBOTG_MEM_BASE 0x38400000
#define AES_MEM_BASE 0x38C00000
#define VIC0_MEM_BASE 0x38E00000
#define VIC1_MEM_BASE 0x38E01000
#define SYSIC_MEM_BASE  0x39700000
#define DMAC1_MEM_BASE 0x39900000
#define SPI0_MEM_BASE 0x3C300000
#define USBPHYS_MEM_BASE 0x3C400000
#define CLOCK0_MEM_BASE 0x3C500000
#define TIMER1_MEM_BASE 0x3C700000
#define SPI1_MEM_BASE 0x3CE00000
#define GPIO_MEM_BASE   0x3CF00000
#define PKE_MEM_BASE 0x3D000000
#define CHIPID_MEM_BASE 0x3D100000
#define SPI2_MEM_BASE 0x3D200000
#define SPI3_MEM_BASE 0x3DA00000
#define CLOCK1_MEM_BASE 0x3E000000
#define SPI4_MEM_BASE 0x3E100000

typedef struct {
    MachineClass parent;
} IPodTouchMachineClass;

typedef struct {
	MachineState parent;
	AddressSpace *nsas;
	qemu_irq **irq;
	ARMCPU *cpu;
	PL192State *vic0;
	PL192State *vic1;
	IPodTouchClockState *clock0;
	IPodTouchClockState *clock1;
	IPodTouchTimerState *timer1;
	IPodTouchChipIDState *chipid_state;
	IPodTouchGPIOState *gpio_state;
	IPodTouchSYSICState *sysic;
	synopsys_usb_state *usb_otg;
	IPodTouchUSBPhysState *usb_phys_state;
	IPodTouchSHA1State *sha1_state;
	IPodTouchAESState *aes_state;
	IPodTouchPKEState *pke_state;
	Clock *sysclk;
	char nor_path[1024];
	IT2G_CPREG_VAR_DEF(REG0);
	IT2G_CPREG_VAR_DEF(REG1);
	
	
} IPodTouchMachineState;

#endif