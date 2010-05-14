#ifndef MC146818RTC_H
#define MC146818RTC_H

#include "isa.h"

ISADevice *rtc_init(int base_year);
void rtc_set_memory(ISADevice *dev, int addr, int val);
void rtc_set_date(ISADevice *dev, const struct tm *tm);

#endif /* !MC146818RTC_H */
