/*
 * RTC configuration and clock read
 *
 * Copyright (c) 2003-2021 QEMU contributors
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

#ifndef SYSTEM_RTC_H
#define SYSTEM_RTC_H

/**
 * qemu_get_timedate: Get the current RTC time
 * @tm: struct tm to fill in with RTC time
 * @offset: offset in seconds to adjust the RTC time by before
 *          converting to struct tm format.
 *
 * This function fills in @tm with the current RTC time, as adjusted
 * by @offset (for example, if @offset is 3600 then the returned time/date
 * will be one hour further ahead than the current RTC time).
 *
 * The usual use is by RTC device models, which should call this function
 * to find the time/date value that they should return to the guest
 * when it reads the RTC registers.
 *
 * The behaviour of the clock whose value this function returns will
 * depend on the -rtc command line option passed by the user.
 */
void qemu_get_timedate(struct tm *tm, time_t offset);

/**
 * qemu_timedate_diff: Return difference between a struct tm and the RTC
 * @tm: struct tm containing the date/time to compare against
 *
 * Returns the difference in seconds between the RTC clock time
 * and the date/time specified in @tm. For example, if @tm specifies
 * a timestamp one hour further ahead than the current RTC time
 * then this function will return 3600.
 */
time_t qemu_timedate_diff(struct tm *tm);

#endif
