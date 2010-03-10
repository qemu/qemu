#ifndef QEMU_TIMER_H
#define QEMU_TIMER_H

/* timers */

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

int64_t qemu_get_clock(QEMUClock *clock);
int64_t qemu_get_clock_ns(QEMUClock *clock);
void qemu_clock_enable(QEMUClock *clock, int enabled);

QEMUTimer *qemu_new_timer(QEMUClock *clock, QEMUTimerCB *cb, void *opaque);
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

static inline int64_t get_ticks_per_sec(void)
{
    return 1000000000LL;
}


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

#endif
