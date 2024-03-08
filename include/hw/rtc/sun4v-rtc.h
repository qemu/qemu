/*
 * QEMU sun4v Real Time Clock device
 *
 * The sun4v_rtc device (sun4v tod clock)
 *
 * Copyright (c) 2016 Artyom Tarasenko
 *
 * This code is licensed under the GNU GPL v2 or (at your option) any later
 * version.
 */

#ifndef HW_RTC_SUN4V_RTC_H
#define HW_RTC_SUN4V_RTC_H

#include "exec/hwaddr.h"

void sun4v_rtc_init(hwaddr addr);

#endif
