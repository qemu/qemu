#ifndef HW_ARM_IPOD_TOUCH_USB_PHYS_H
#define HW_ARM_IPOD_TOUCH_USB_PHYS_H

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/sysbus.h"

#define TYPE_IPOD_TOUCH_USB_PHYS "ipodtouch.usbphys"
OBJECT_DECLARE_SIMPLE_TYPE(IPodTouchUSBPhysState, IPOD_TOUCH_USB_PHYS)

#define REG_OPHYPWR 0x0
#define REG_OPHYCLK 0x4
#define REG_ORSTCON 0x8
#define REG_UNKNOWN1 0x1C
#define REG_OPHYTUNE 0x20

typedef struct IPodTouchUSBPhysState {
    SysBusDevice busdev;
    MemoryRegion iomem;

    uint32_t usb_ophypwr;
    uint32_t usb_ophyclk;
    uint32_t usb_orstcon;
    uint32_t usb_unknown1;
    uint32_t usb_ophytune;
} IPodTouchUSBPhysState;

#endif