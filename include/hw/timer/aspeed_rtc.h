/*
 * ASPEED Real Time Clock
 * Joel Stanley <joel@jms.id.au>
 *
 * Copyright 2019 IBM Corp
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef ASPEED_RTC_H
#define ASPEED_RTC_H

#include <stdint.h>

#include "hw/hw.h"
#include "hw/irq.h"
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

#endif /* ASPEED_RTC_H */
