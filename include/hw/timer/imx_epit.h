/*
 * i.MX EPIT Timer
 *
 * Copyright (c) 2008 OK Labs
 * Copyright (c) 2011 NICTA Pty Ltd
 * Originally written by Hans Jiang
 * Updated by Peter Chubb
 * Updated by Jean-Christophe Dubois <jcd@tribudubois.net>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef IMX_EPIT_H
#define IMX_EPIT_H

#include "hw/sysbus.h"
#include "hw/ptimer.h"
#include "hw/misc/imx_ccm.h"
#include "qom/object.h"

/*
 * EPIT: Enhanced periodic interrupt timer
 */

#define CR_EN       (1 << 0)
#define CR_ENMOD    (1 << 1)
#define CR_OCIEN    (1 << 2)
#define CR_RLD      (1 << 3)
#define CR_PRESCALE_SHIFT (4)
#define CR_PRESCALE_BITS  (12)
#define CR_SWR      (1 << 16)
#define CR_IOVW     (1 << 17)
#define CR_DBGEN    (1 << 18)
#define CR_WAITEN   (1 << 19)
#define CR_DOZEN    (1 << 20)
#define CR_STOPEN   (1 << 21)
#define CR_CLKSRC_SHIFT (24)
#define CR_CLKSRC_BITS  (2)

#define SR_OCIF     (1 << 0)

#define EPIT_TIMER_MAX  0XFFFFFFFFUL

#define TYPE_IMX_EPIT "imx.epit"
OBJECT_DECLARE_SIMPLE_TYPE(IMXEPITState, IMX_EPIT)

struct IMXEPITState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    ptimer_state *timer_reload;
    ptimer_state *timer_cmp;
    MemoryRegion  iomem;
    IMXCCMState  *ccm;

    uint32_t cr;
    uint32_t sr;
    uint32_t lr;
    uint32_t cmp;

    qemu_irq irq;
};

#endif /* IMX_EPIT_H */
