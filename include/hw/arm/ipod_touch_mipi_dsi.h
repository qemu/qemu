#ifndef IPOD_TOUCH_MIPI_DSI_H
#define IPOD_TOUCH_MIPI_DSI_H

#include <math.h>
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "hw/sysbus.h"
#include "hw/irq.h"

#define TYPE_IPOD_TOUCH_MIPI_DSI                "ipodtouch.mipidsi"
OBJECT_DECLARE_SIMPLE_TYPE(IPodTouchMIPIDSIState, IPOD_TOUCH_MIPI_DSI)

#define REG_INTSRC   0x2C
#define REG_PKTHDR   0x34
#define REG_RXFIFO   0x3C
#define REG_FIFOCTRL 0x44

#define DSIM_RSP_LONG_READ 0x1A
#define rDSIM_FIFOCTRL_EmptyHSfr 0x400000
#define rDSIM_STATUS_TxReadyHsClk 0x400
#define rDSIM_INTSRC_RxDatDone    0x00040000

typedef struct IPodTouchMIPIDSIState
{
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    qemu_irq irq;
    uint32_t pkthdr_reg;
    bool return_panel_id;
} IPodTouchMIPIDSIState;

#endif