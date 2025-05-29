/*
 * QEMU monitor
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
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

#include "qemu/osdep.h"
#include "monitor/monitor.h"
#include "qobject/qdict.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-misc-i386.h"
#include "hw/i386/x86.h"
#include "hw/rtc/mc146818rtc.h"

#include CONFIG_DEVICES

void qmp_rtc_reset_reinjection(Error **errp)
{
    X86MachineState *x86ms = X86_MACHINE(qdev_get_machine());

#ifdef CONFIG_MC146818RTC
    if (x86ms->rtc) {
        rtc_reset_reinjection(MC146818_RTC(x86ms->rtc));
    }
#else
    assert(!x86ms->rtc);
#endif
}
