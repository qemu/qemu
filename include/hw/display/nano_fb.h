/*
 * Copyright (c) 2020 Nanosonics
 *
 * Nanosonics IMX6UL LCDIF emulation.
 *
 *
 * This code is licensed under the GPL, version 2 or later.
 * See the file `COPYING' in the top level directory.
 *
 * It (partially) emulates nanosonics platform with a Freescale
 * i.MX6ul SoC
 */

#ifndef NANO_FB_H
#define NANO_FB_H
#include "ui/input.h"
#include "ui/console.h"
#include "hw/irq.h"
#include "hw/sysbus.h"

#define TYPE_NANOFB "nano_fb"
#define NANO_LCD_DEV_NAME "nano_lcd"

typedef enum IndicatorLedStatus
{
    eOff = 0,
    eRed,
    eGreen,
}IndicatorLedStatus;

struct NANOFbState {
    SysBusDevice        parent_obj;

    MemoryRegion        iomem;
    MemoryRegionSection fbsection;

    //screen
    QemuConsole *       con;
    int                 con_inited;

    //elcdif related
    qemu_irq            elcdif_irq;
	unsigned int	    ctrl;
	unsigned int	    ctrl_set;
	unsigned int	    ctrl_clr;
    unsigned int        ctrl1;
	unsigned int	    ctrl1_set;
	unsigned int	    ctrl1_clr;
	unsigned int	    w;
    unsigned int        h;
    unsigned int        cur_buf;
    unsigned int        timing;

    int             invalidate;

    //input
    QemuInputHandlerState * input;
    int axis[INPUT_AXIS__MAX];

    //led related
    IndicatorLedStatus indicatorLed;
    bool               startBtnLedOn;

};
typedef struct NANOFbState NANOFbState;

#define NANOFB(obj) OBJECT_CHECK(NANOFbState, (obj), TYPE_NANOFB)

void updateRGBLedStatus(IndicatorLedStatus ledStatus);
void updateStartButtonLedStatus(bool bOn);


#endif
