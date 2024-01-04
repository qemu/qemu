#ifndef KVMCLOCK_H
#define KVMCLOCK_H

#define MSR_KVM_WALL_CLOCK  0x11
#define MSR_KVM_SYSTEM_TIME 0x12

#define MAX_CPU 64

#define PVCLOCK_TSC_STABLE_BIT (1 << 0)
#define PVCLOCK_RAW_CYCLE_BIT (1 << 7) /* Get raw cycle */

# define NSEC_PER_SEC			1000000000ULL

typedef u64 cycle_t;

struct pvclock_vcpu_time_info {
	u32   version;
	u32   pad0;
	u64   tsc_timestamp;
	u64   system_time;
	u32   tsc_to_system_mul;
	s8    tsc_shift;
	u8    flags;
	u8    pad[2];
} __attribute__((__packed__)); /* 32 bytes */

struct pvclock_wall_clock {
	u32   version;
	u32   sec;
	u32   nsec;
} __attribute__((__packed__));

/*
 * These are perodically updated
 *    xen: magic shared_info page
 *    kvm: gpa registered via msr
 * and then copied here.
 */
struct pvclock_shadow_time {
	u64 tsc_timestamp;     /* TSC at last update of time vals.  */
	u64 system_timestamp;  /* Time, in nanosecs, since boot.    */
	u32 tsc_to_nsec_mul;
	int tsc_shift;
	u32 version;
	u8  flags;
};


struct timespec {
        long   tv_sec;
        long   tv_nsec;
};

void pvclock_set_flags(unsigned char flags);
cycle_t kvm_clock_read();
void kvm_get_wallclock(struct timespec *ts);
void kvm_clock_init(void *data);
void kvm_clock_clear(void *data);

#endif
