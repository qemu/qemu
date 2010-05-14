#ifndef MC146818RTC_H
#define MC146818RTC_H

typedef struct RTCState RTCState;

RTCState *rtc_init(int base_year);
void rtc_set_memory(RTCState *s, int addr, int val);
void rtc_set_date(RTCState *s, const struct tm *tm);

#endif /* !MC146818RTC_H */
