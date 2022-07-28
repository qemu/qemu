/*
 * AllWinner F1 display emulation
 * Written by froloff
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_ALLWINNER_F1_DISP_H
#define HW_ALLWINNER_F1_DISP_H

#include "hw/sysbus.h"
#include "ui/console.h"
#include "qom/object.h"

#define TYPE_AW_F1_DEBE "aw-f1-debe"
OBJECT_DECLARE_SIMPLE_TYPE(AwF1DEBEState, AW_F1_DEBE)

/** Total number of known registers */
#define AW_DEBE_REGS_NUM     (0x180 / sizeof(uint32_t))

/*
 * Configuration information about the fb which the guest can program
 */

struct AwF1DEBEState {
    /*< private >*/
    SysBusDevice  busdev;
    /*< public >*/

    QemuConsole * con;
    uint32_t      ctl;
    uint32_t      fb0_base;
    uint16_t      xres;
    uint16_t      yres;
    uint16_t      pix0_fmt;
    uint16_t      pix0_opts;
    bool          invalidate;

    MemoryRegion        iomem;
    MemoryRegionSection fbsection;   
    uint32_t            regs[AW_DEBE_REGS_NUM];    
};


#endif
