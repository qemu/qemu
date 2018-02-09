/*
 * i.MX GPT Timer
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

#ifndef IMX_GPT_H
#define IMX_GPT_H

#include "hw/sysbus.h"
#include "hw/ptimer.h"
#include "hw/misc/imx_ccm.h"

/*
 * GPT : General purpose timer
 *
 * This timer counts up continuously while it is enabled, resetting itself
 * to 0 when it reaches GPT_TIMER_MAX (in freerun mode) or when it
 * reaches the value of one of the ocrX (in periodic mode).
 */

#define GPT_TIMER_MAX  0XFFFFFFFFUL

/* Control register.  Not all of these bits have any effect (yet) */
#define GPT_CR_EN     (1 << 0)  /* GPT Enable */
#define GPT_CR_ENMOD  (1 << 1)  /* GPT Enable Mode */
#define GPT_CR_DBGEN  (1 << 2)  /* GPT Debug mode enable */
#define GPT_CR_WAITEN (1 << 3)  /* GPT Wait Mode Enable  */
#define GPT_CR_DOZEN  (1 << 4)  /* GPT Doze mode enable */
#define GPT_CR_STOPEN (1 << 5)  /* GPT Stop Mode Enable */
#define GPT_CR_CLKSRC_SHIFT (6)
#define GPT_CR_CLKSRC_MASK  (0x7)

#define GPT_CR_FRR    (1 << 9)  /* Freerun or Restart */
#define GPT_CR_SWR    (1 << 15) /* Software Reset */
#define GPT_CR_IM1    (3 << 16) /* Input capture channel 1 mode (2 bits) */
#define GPT_CR_IM2    (3 << 18) /* Input capture channel 2 mode (2 bits) */
#define GPT_CR_OM1    (7 << 20) /* Output Compare Channel 1 Mode (3 bits) */
#define GPT_CR_OM2    (7 << 23) /* Output Compare Channel 2 Mode (3 bits) */
#define GPT_CR_OM3    (7 << 26) /* Output Compare Channel 3 Mode (3 bits) */
#define GPT_CR_FO1    (1 << 29) /* Force Output Compare Channel 1 */
#define GPT_CR_FO2    (1 << 30) /* Force Output Compare Channel 2 */
#define GPT_CR_FO3    (1 << 31) /* Force Output Compare Channel 3 */

#define GPT_SR_OF1  (1 << 0)
#define GPT_SR_OF2  (1 << 1)
#define GPT_SR_OF3  (1 << 2)
#define GPT_SR_ROV  (1 << 5)

#define GPT_IR_OF1IE  (1 << 0)
#define GPT_IR_OF2IE  (1 << 1)
#define GPT_IR_OF3IE  (1 << 2)
#define GPT_IR_ROVIE  (1 << 5)

#define TYPE_IMX25_GPT "imx25.gpt"
#define TYPE_IMX31_GPT "imx31.gpt"
#define TYPE_IMX6_GPT "imx6.gpt"
#define TYPE_IMX7_GPT "imx7.gpt"

#define TYPE_IMX_GPT TYPE_IMX25_GPT

#define IMX_GPT(obj) OBJECT_CHECK(IMXGPTState, (obj), TYPE_IMX_GPT)

typedef struct IMXGPTState{
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    ptimer_state *timer;
    MemoryRegion  iomem;
    IMXCCMState  *ccm;

    uint32_t cr;
    uint32_t pr;
    uint32_t sr;
    uint32_t ir;
    uint32_t ocr1;
    uint32_t ocr2;
    uint32_t ocr3;
    uint32_t icr1;
    uint32_t icr2;
    uint32_t cnt;

    uint32_t next_timeout;
    uint32_t next_int;

    uint32_t freq;

    qemu_irq irq;

    const IMXClk *clocks;
} IMXGPTState;

#endif /* IMX_GPT_H */
