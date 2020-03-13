#ifndef IMX_USB_PHY_H
#define IMX_USB_PHY_H

#include "hw/sysbus.h"
#include "qemu/bitops.h"

enum IMXUsbPhyRegisters {
    USBPHY_PWD,
    USBPHY_PWD_SET,
    USBPHY_PWD_CLR,
    USBPHY_PWD_TOG,
    USBPHY_TX,
    USBPHY_TX_SET,
    USBPHY_TX_CLR,
    USBPHY_TX_TOG,
    USBPHY_RX,
    USBPHY_RX_SET,
    USBPHY_RX_CLR,
    USBPHY_RX_TOG,
    USBPHY_CTRL,
    USBPHY_CTRL_SET,
    USBPHY_CTRL_CLR,
    USBPHY_CTRL_TOG,
    USBPHY_STATUS,
    USBPHY_DEBUG = 0x14,
    USBPHY_DEBUG_SET,
    USBPHY_DEBUG_CLR,
    USBPHY_DEBUG_TOG,
    USBPHY_DEBUG0_STATUS,
    USBPHY_DEBUG1 = 0x1c,
    USBPHY_DEBUG1_SET,
    USBPHY_DEBUG1_CLR,
    USBPHY_DEBUG1_TOG,
    USBPHY_VERSION,
    USBPHY_MAX
};

#define USBPHY_CTRL_SFTRST BIT(31)

#define TYPE_IMX_USBPHY "imx.usbphy"
#define IMX_USBPHY(obj) OBJECT_CHECK(IMXUSBPHYState, (obj), TYPE_IMX_USBPHY)

typedef struct IMXUSBPHYState {
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */
    MemoryRegion iomem;

    uint32_t usbphy[USBPHY_MAX];
} IMXUSBPHYState;

#endif /* IMX_USB_PHY_H */
