#ifndef QEMU_TIMER_H
#define QEMU_TIMER_H

#include "qemu-common.h"
#include <time.h>
#include <sys/time.h>

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#endif

/* timers */

#define SCALE_MS 1000000
#define SCALE_US 1000
#define SCALE_NS 1

typedef struct QEMUClock QEMUClock;
typedef void QEMUTimerCB(void *opaque);

/* The real time clock should be used only for stuff which does not
   change the virtual machine state, as it is run even if the virtual
   machine is stopped. The real time clock has a frequency of 1000
   Hz. */
extern QEMUClock *rt_clock;

/* The virtual clock is only run during the emulation. It is stopped
   when the virtual machine is stopped. Virtual timers use a high
   precision clock, usually cpu cycles (use ticks_per_sec). */
extern QEMUClock *vm_clock;

/* The host clock should be use for device models that emulate accurate
   real time sources. It will continue to run when the virtual machine
   is suspended, and it will reflect system time changes the host may
   undergo (e.g. due to NTP). The host clock has the same precision as
   the virtual clock. */
extern QEMUClock *host_clock;

int64_t qemu_get_clock_ns(QEMUClock *clock);
void qemu_clock_enable(QEMUClock *clock, int enabled);
void qemu_clock_warp(QEMUClock *clock);

QEMUTimer *qemu_new_timer(QEMUClock *clock, int scale,
                          QEMUTimerCB *cb, void *opaque);
void qemu_free_timer(QEMUTimer *ts);
void qemu_del_timer(QEMUTimer *ts);
void qemu_mod_timer(QEMUTimer *ts, int64_t expire_time);
int qemu_timer_pending(QEMUTimer *ts);
int qemu_timer_expired(QEMUTimer *timer_head, int64_t current_time);

void qemu_run_all_timers(void);
int qemu_alarm_pending(void);
int64_t qemu_next_deadline(void);
void configure_alarms(char const *opt);
void configure_icount(const char *option);
int qemu_calculate_timeout(void);
void init_clocks(void);
int init_timer_alarm(void);
void quit_timers(void);

static inline QEMUTimer *qemu_new_timer_ns(QEMUClock *clock, QEMUTimerCB *cb,
                                           void *opaque)
{
    return qemu_new_timer(clock, SCALE_NS, cb, opaque);
}

static inline QEMUTimer *qemu_new_timer_ms(QEMUClock *clock, QEMUTimerCB *cb,
                                           void *opaque)
{
    return qemu_new_timer(clock, SCALE_MS, cb, opaque);
}

static inline int64_t qemu_get_clock_ms(QEMUClock *clock)
{
    return qemu_get_clock_ns(clock) / SCALE_MS;
}

static inline int64_t get_ticks_per_sec(void)
{
    return 1000000000LL;
}

/* real time host monotonic timer */
static inline int64_t get_clock_realtime(void)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000000LL + (tv.tv_usec * 1000);
}

/* Warning: don't insert tracepoints into these functions, they are
   also used by simpletrace backend and tracepoints would cause
   an infinite recursion! */
#ifdef _WIN32
extern int64_t clock_freq;

static inline int64_t get_clock(void)
{
    LARGE_INTEGER ti;
    QueryPerformanceCounter(&ti);
    return muldiv64(ti.QuadPart, get_ticks_per_sec(), clock_freq);
}

#else

extern int use_rt_clock;

static inline int64_t get_clock(void)
{
#if defined(__linux__) || (defined(__FreeBSD__) && __FreeBSD_version >= 500000) \
    || defined(__DragonFly__) || defined(__FreeBSD_kernel__)
    if (use_rt_clock) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return ts.tv_sec * 1000000000LL + ts.tv_nsec;
    } else
#endif
    {
        /* XXX: using gettimeofday leads to problems if the date
           changes, so it should be avoided. */
        return get_clock_realtime();
    }
}
#endif

void qemu_get_timer(QEMUFile *f, QEMUTimer *ts);
void qemu_put_timer(QEMUFile *f, QEMUTimer *ts);

/* ptimer.c */
typedef struct ptimer_state ptimer_state;
typedef void (*ptimer_cb)(void *opaque);

ptimer_state *ptimer_init(QEMUBH *bh);
void ptimer_set_period(ptimer_state *s, int64_t period);
void ptimer_set_freq(ptimer_state *s, uint32_t freq);
void ptimer_set_limit(ptimer_state *s, uint64_t limit, int reload);
uint64_t ptimer_get_count(ptimer_state *s);
void ptimer_set_count(ptimer_state *s, uint64_t count);
void ptimer_run(ptimer_state *s, int oneshot);
void ptimer_stop(ptimer_state *s);
void qemu_put_ptimer(QEMUFile *f, ptimer_state *s);
void qemu_get_ptimer(QEMUFile *f, ptimer_state *s);

/* icount */
int64_t qemu_icount_round(int64_t count);
extern int64_t qemu_icount;
extern int use_icount;
extern int icount_time_shift;
extern int64_t qemu_icount_bias;
int64_t cpu_get_icount(void);

/*******************************************/
/* host CPU ticks (if available) */

#if defined(_ARCH_PPC)

static inline int64_t cpu_get_real_ticks(void)
{
    int64_t retval;
#ifdef _ARCH_PPC64
    /* This reads timebase in one 64bit go and includes Cell workaround from:
       http://ozlabs.org/pipermail/linuxppc-dev/2006-October/027052.html
    */
    __asm__ __volatile__ ("mftb    %0\n\t"
                          "cmpwi   %0,0\n\t"
                          "beq-    $-8"
                          : "=r" (retval));
#else
    /* http://ozlabs.org/pipermail/linuxppc-dev/1999-October/003889.html */
    unsigned long junk;
    __asm__ __volatile__ ("mfspr   %1,269\n\t"  /* mftbu */
                          "mfspr   %L0,268\n\t" /* mftb */
                          "mfspr   %0,269\n\t"  /* mftbu */
                          "cmpw    %0,%1\n\t"
                          "bne     $-16"
                          : "=r" (retval), "=r" (junk));
#endif
    return retval;
}

#elif defined(__i386__)

static inline int64_t cpu_get_real_ticks(void)
{
    int64_t val;
    asm volatile ("rdtsc" : "=A" (val));
    return val;
}

#elif defined(__x86_64__)

static inline int64_t cpu_get_real_ticks(void)
{
    uint32_t low,high;
    int64_t val;
    asm volatile("rdtsc" : "=a" (low), "=d" (high));
    val = high;
    val <<= 32;
    val |= low;
    return val;
}

#elif defined(__hppa__)

static inline int64_t cpu_get_real_ticks(void)
{
    int val;
    asm volatile ("mfctl %%cr16, %0" : "=r"(val));
    return val;
}

#elif defined(__ia64)

static inline int64_t cpu_get_real_ticks(void)
{
    int64_t val;
    asm volatile ("mov %0 = ar.itc" : "=r"(val) :: "memory");
    return val;
}

#elif defined(__s390__)

static inline int64_t cpu_get_real_ticks(void)
{
    int64_t val;
    asm volatile("stck 0(%1)" : "=m" (val) : "a" (&val) : "cc");
    return val;
}

#elif defined(__sparc_v8plus__) || defined(__sparc_v8plusa__) || defined(__sparc_v9__)

static inline int64_t cpu_get_real_ticks (void)
{
#if defined(_LP64)
    uint64_t        rval;
    asm volatile("rd %%tick,%0" : "=r"(rval));
    return rval;
#else
    union {
        uint64_t i64;
        struct {
            uint32_t high;
            uint32_t low;
        }       i32;
    } rval;
    asm volatile("rd %%tick,%1; srlx %1,32,%0"
                 : "=r"(rval.i32.high), "=r"(rval.i32.low));
    return rval.i64;
#endif
}

#elif defined(__mips__) && \
    ((defined(__mips_isa_rev) && __mips_isa_rev >= 2) || defined(__linux__))
/*
 * binutils wants to use rdhwr only on mips32r2
 * but as linux kernel emulate it, it's fine
 * to use it.
 *
 */
#define MIPS_RDHWR(rd, value) {                         \
        __asm__ __volatile__ (".set   push\n\t"         \
                              ".set mips32r2\n\t"       \
                              "rdhwr  %0, "rd"\n\t"     \
                              ".set   pop"              \
                              : "=r" (value));          \
    }

static inline int64_t cpu_get_real_ticks(void)
{
    /* On kernels >= 2.6.25 rdhwr <reg>, $2 and $3 are emulated */
    uint32_t count;
    static uint32_t cyc_per_count = 0;

    if (!cyc_per_count) {
        MIPS_RDHWR("$3", cyc_per_count);
    }

    MIPS_RDHWR("$2", count);
    return (int64_t)(count * cyc_per_count);
}

#elif defined(__alpha__)

static inline int64_t cpu_get_real_ticks(void)
{
    uint64_t cc;
    uint32_t cur, ofs;

    asm volatile("rpcc %0" : "=r"(cc));
    cur = cc;
    ofs = cc >> 32;
    return cur - ofs;
}

#else
/* The host CPU doesn't have an easily accessible cycle counter.
   Just return a monotonically increasing value.  This will be
   totally wrong, but hopefully better than nothing.  */
static inline int64_t cpu_get_real_ticks (void)
{
    static int64_t ticks = 0;
    return ticks++;
}
#endif

#ifdef NEED_CPU_H
/* Deterministic execution requires that IO only be performed on the last
   instruction of a TB so that interrupts take effect immediately.  */
static inline int can_do_io(CPUState *env)
{
    if (!use_icount)
        return 1;

    /* If not executing code then assume we are ok.  */
    if (!env->current_tb)
        return 1;

    return env->can_do_io != 0;
}
#endif

#ifdef CONFIG_PROFILER
static inline int64_t profile_getclock(void)
{
    return cpu_get_real_ticks();
}

extern int64_t qemu_time, qemu_time_start;
extern int64_t tlb_flush_time;
extern int64_t dev_time;
#endif

#endif
