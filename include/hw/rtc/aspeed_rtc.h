/*
 * ASPEED Real Time Clock
 * Joel Stanley <joel@jms.id.au>
 *
 * Copyright 2019 IBM Corp
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_RTC_ASPEED_RTC_H
#define HW_RTC_ASPEED_RTC_H

#include "hw/sysbus.h"

typedef struct AspeedRtcState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;

    uint32_t reg[0x18];
    int offset;

} AspeedRtcState;

#define TYPE_ASPEED_RTC "aspeed.rtc"
#define ASPEED_RTC(obj) OBJECT_CHECK(AspeedRtcState, (obj), TYPE_ASPEED_RTC)

#endif /* HW_RTC_ASPEED_RTC_H */
