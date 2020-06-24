#ifndef TIME_H
#define TIME_H

static inline u64 get_clock(void)
{
    u64 r;

    asm volatile("stck %0" : "=Q" (r) : : "cc");
    return r;
}

static inline u64 get_time_ms(void)
{
    /* Bit 51 is incremented each microsecond */
    return (get_clock() >> 12) / 1000;
}

static inline u64 get_time_seconds(void)
{
    return get_time_ms() / 1000;
}

#endif
