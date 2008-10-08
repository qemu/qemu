/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
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
#include "hw/hw.h"
#include "hw/boards.h"
#include "hw/usb.h"
#include "hw/pcmcia.h"
#include "hw/pc.h"
#include "hw/audiodev.h"
#include "hw/isa.h"
#include "hw/baum.h"
#include "hw/bt.h"
#include "net.h"
#include "console.h"
#include "sysemu.h"
#include "gdbstub.h"
#include "qemu-timer.h"
#include "qemu-char.h"
#include "block.h"
#include "audio/audio.h"

#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <sys/time.h>
#include <zlib.h>

#ifndef _WIN32
#include <sys/times.h>
#include <sys/wait.h>
#include <termios.h>
#include <sys/poll.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <dirent.h>
#include <netdb.h>
#include <sys/select.h>
#include <arpa/inet.h>
#ifdef _BSD
#include <sys/stat.h>
#if !defined(__APPLE__) && !defined(__OpenBSD__)
#include <libutil.h>
#endif
#ifdef __OpenBSD__
#include <net/if.h>
#endif
#elif defined (__GLIBC__) && defined (__FreeBSD_kernel__)
#include <freebsd/stdlib.h>
#else
#ifdef __linux__
#include <linux/if.h>
#include <linux/if_tun.h>
#include <pty.h>
#include <malloc.h>
#include <linux/rtc.h>

/* For the benefit of older linux systems which don't supply it,
   we use a local copy of hpet.h. */
/* #include <linux/hpet.h> */
#include "hpet.h"

#include <linux/ppdev.h>
#include <linux/parport.h>
#endif
#ifdef __sun__
#include <sys/stat.h>
#include <sys/ethernet.h>
#include <sys/sockio.h>
#include <netinet/arp.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h> // must come after ip.h
#include <netinet/udp.h>
#include <netinet/tcp.h>
#include <net/if.h>
#include <syslog.h>
#include <stropts.h>
#endif
#endif
#endif

#include "qemu_socket.h"

#if defined(CONFIG_SLIRP)
#include "libslirp.h"
#endif

#if defined(__OpenBSD__)
#include <util.h>
#endif

#if defined(CONFIG_VDE)
#include <libvdeplug.h>
#endif

#ifdef _WIN32
#include <malloc.h>
#include <sys/timeb.h>
#include <mmsystem.h>
#define getopt_long_only getopt_long
#define memalign(align, size) malloc(size)
#endif

#ifdef CONFIG_SDL
#ifdef __APPLE__
#include <SDL/SDL.h>
#endif
#endif /* CONFIG_SDL */

#ifdef CONFIG_COCOA
#undef main
#define main qemu_main
#endif /* CONFIG_COCOA */

#include "disas.h"

#include "exec-all.h"

#define DEFAULT_NETWORK_SCRIPT "/etc/qemu-ifup"
#define DEFAULT_NETWORK_DOWN_SCRIPT "/etc/qemu-ifdown"
#ifdef __sun__
#define SMBD_COMMAND "/usr/sfw/sbin/smbd"
#else
#define SMBD_COMMAND "/usr/sbin/smbd"
#endif

//#define DEBUG_UNUSED_IOPORT
//#define DEBUG_IOPORT
//#define DEBUG_NET
//#define DEBUG_SLIRP

#ifdef TARGET_PPC
#define DEFAULT_RAM_SIZE 144
#else
#define DEFAULT_RAM_SIZE 128
#endif

/* Max number of USB devices that can be specified on the commandline.  */
#define MAX_USB_CMDLINE 8

/* XXX: use a two level table to limit memory usage */
#define MAX_IOPORTS 65536

const char *bios_dir = CONFIG_QEMU_SHAREDIR;
const char *bios_name = NULL;
static void *ioport_opaque[MAX_IOPORTS];
static IOPortReadFunc *ioport_read_table[3][MAX_IOPORTS];
static IOPortWriteFunc *ioport_write_table[3][MAX_IOPORTS];
/* Note: drives_table[MAX_DRIVES] is a dummy block driver if none available
   to store the VM snapshots */
DriveInfo drives_table[MAX_DRIVES+1];
int nb_drives;
/* point to the block driver where the snapshots are managed */
static BlockDriverState *bs_snapshots;
static int vga_ram_size;
enum vga_retrace_method vga_retrace_method = VGA_RETRACE_DUMB;
static DisplayState display_state;
int nographic;
static int curses;
const char* keyboard_layout = NULL;
int64_t ticks_per_sec;
ram_addr_t ram_size;
int nb_nics;
NICInfo nd_table[MAX_NICS];
int vm_running;
static int rtc_utc = 1;
static int rtc_date_offset = -1; /* -1 means no change */
int cirrus_vga_enabled = 1;
int vmsvga_enabled = 0;
#ifdef TARGET_SPARC
int graphic_width = 1024;
int graphic_height = 768;
int graphic_depth = 8;
#else
int graphic_width = 800;
int graphic_height = 600;
int graphic_depth = 15;
#endif
static int full_screen = 0;
static int no_frame = 0;
int no_quit = 0;
CharDriverState *serial_hds[MAX_SERIAL_PORTS];
CharDriverState *parallel_hds[MAX_PARALLEL_PORTS];
#ifdef TARGET_I386
int win2k_install_hack = 0;
#endif
int usb_enabled = 0;
static VLANState *first_vlan;
int smp_cpus = 1;
const char *vnc_display;
int acpi_enabled = 1;
int fd_bootchk = 1;
int no_reboot = 0;
int no_shutdown = 0;
int cursor_hide = 1;
int graphic_rotate = 0;
int daemonize = 0;
const char *option_rom[MAX_OPTION_ROMS];
int nb_option_roms;
int semihosting_enabled = 0;
#ifdef TARGET_ARM
int old_param = 0;
#endif
const char *qemu_name;
int alt_grab = 0;
#ifdef TARGET_SPARC
unsigned int nb_prom_envs = 0;
const char *prom_envs[MAX_PROM_ENVS];
#endif
static int nb_drives_opt;
static struct drive_opt {
    const char *file;
    char opt[1024];
} drives_opt[MAX_DRIVES];

static CPUState *cur_cpu;
static CPUState *next_cpu;
static int event_pending = 1;
/* Conversion factor from emulated instructions to virtual clock ticks.  */
static int icount_time_shift;
/* Arbitrarily pick 1MIPS as the minimum allowable speed.  */
#define MAX_ICOUNT_SHIFT 10
/* Compensate for varying guest execution speed.  */
static int64_t qemu_icount_bias;
static QEMUTimer *icount_rt_timer;
static QEMUTimer *icount_vm_timer;

uint8_t qemu_uuid[16];

#define TFR(expr) do { if ((expr) != -1) break; } while (errno == EINTR)

/***********************************************************/
/* x86 ISA bus support */

target_phys_addr_t isa_mem_base = 0;
PicState2 *isa_pic;

static IOPortReadFunc default_ioport_readb, default_ioport_readw, default_ioport_readl;
static IOPortWriteFunc default_ioport_writeb, default_ioport_writew, default_ioport_writel;

static uint32_t ioport_read(int index, uint32_t address)
{
    static IOPortReadFunc *default_func[3] = {
        default_ioport_readb,
        default_ioport_readw,
        default_ioport_readl
    };
    IOPortReadFunc *func = ioport_read_table[index][address];
    if (!func)
        func = default_func[index];
    return func(ioport_opaque[address], address);
}

static void ioport_write(int index, uint32_t address, uint32_t data)
{
    static IOPortWriteFunc *default_func[3] = {
        default_ioport_writeb,
        default_ioport_writew,
        default_ioport_writel
    };
    IOPortWriteFunc *func = ioport_write_table[index][address];
    if (!func)
        func = default_func[index];
    func(ioport_opaque[address], address, data);
}

static uint32_t default_ioport_readb(void *opaque, uint32_t address)
{
#ifdef DEBUG_UNUSED_IOPORT
    fprintf(stderr, "unused inb: port=0x%04x\n", address);
#endif
    return 0xff;
}

static void default_ioport_writeb(void *opaque, uint32_t address, uint32_t data)
{
#ifdef DEBUG_UNUSED_IOPORT
    fprintf(stderr, "unused outb: port=0x%04x data=0x%02x\n", address, data);
#endif
}

/* default is to make two byte accesses */
static uint32_t default_ioport_readw(void *opaque, uint32_t address)
{
    uint32_t data;
    data = ioport_read(0, address);
    address = (address + 1) & (MAX_IOPORTS - 1);
    data |= ioport_read(0, address) << 8;
    return data;
}

static void default_ioport_writew(void *opaque, uint32_t address, uint32_t data)
{
    ioport_write(0, address, data & 0xff);
    address = (address + 1) & (MAX_IOPORTS - 1);
    ioport_write(0, address, (data >> 8) & 0xff);
}

static uint32_t default_ioport_readl(void *opaque, uint32_t address)
{
#ifdef DEBUG_UNUSED_IOPORT
    fprintf(stderr, "unused inl: port=0x%04x\n", address);
#endif
    return 0xffffffff;
}

static void default_ioport_writel(void *opaque, uint32_t address, uint32_t data)
{
#ifdef DEBUG_UNUSED_IOPORT
    fprintf(stderr, "unused outl: port=0x%04x data=0x%02x\n", address, data);
#endif
}

/* size is the word size in byte */
int register_ioport_read(int start, int length, int size,
                         IOPortReadFunc *func, void *opaque)
{
    int i, bsize;

    if (size == 1) {
        bsize = 0;
    } else if (size == 2) {
        bsize = 1;
    } else if (size == 4) {
        bsize = 2;
    } else {
        hw_error("register_ioport_read: invalid size");
        return -1;
    }
    for(i = start; i < start + length; i += size) {
        ioport_read_table[bsize][i] = func;
        if (ioport_opaque[i] != NULL && ioport_opaque[i] != opaque)
            hw_error("register_ioport_read: invalid opaque");
        ioport_opaque[i] = opaque;
    }
    return 0;
}

/* size is the word size in byte */
int register_ioport_write(int start, int length, int size,
                          IOPortWriteFunc *func, void *opaque)
{
    int i, bsize;

    if (size == 1) {
        bsize = 0;
    } else if (size == 2) {
        bsize = 1;
    } else if (size == 4) {
        bsize = 2;
    } else {
        hw_error("register_ioport_write: invalid size");
        return -1;
    }
    for(i = start; i < start + length; i += size) {
        ioport_write_table[bsize][i] = func;
        if (ioport_opaque[i] != NULL && ioport_opaque[i] != opaque)
            hw_error("register_ioport_write: invalid opaque");
        ioport_opaque[i] = opaque;
    }
    return 0;
}

void isa_unassign_ioport(int start, int length)
{
    int i;

    for(i = start; i < start + length; i++) {
        ioport_read_table[0][i] = default_ioport_readb;
        ioport_read_table[1][i] = default_ioport_readw;
        ioport_read_table[2][i] = default_ioport_readl;

        ioport_write_table[0][i] = default_ioport_writeb;
        ioport_write_table[1][i] = default_ioport_writew;
        ioport_write_table[2][i] = default_ioport_writel;
    }
}

/***********************************************************/

void cpu_outb(CPUState *env, int addr, int val)
{
#ifdef DEBUG_IOPORT
    if (loglevel & CPU_LOG_IOPORT)
        fprintf(logfile, "outb: %04x %02x\n", addr, val);
#endif
    ioport_write(0, addr, val);
#ifdef USE_KQEMU
    if (env)
        env->last_io_time = cpu_get_time_fast();
#endif
}

void cpu_outw(CPUState *env, int addr, int val)
{
#ifdef DEBUG_IOPORT
    if (loglevel & CPU_LOG_IOPORT)
        fprintf(logfile, "outw: %04x %04x\n", addr, val);
#endif
    ioport_write(1, addr, val);
#ifdef USE_KQEMU
    if (env)
        env->last_io_time = cpu_get_time_fast();
#endif
}

void cpu_outl(CPUState *env, int addr, int val)
{
#ifdef DEBUG_IOPORT
    if (loglevel & CPU_LOG_IOPORT)
        fprintf(logfile, "outl: %04x %08x\n", addr, val);
#endif
    ioport_write(2, addr, val);
#ifdef USE_KQEMU
    if (env)
        env->last_io_time = cpu_get_time_fast();
#endif
}

int cpu_inb(CPUState *env, int addr)
{
    int val;
    val = ioport_read(0, addr);
#ifdef DEBUG_IOPORT
    if (loglevel & CPU_LOG_IOPORT)
        fprintf(logfile, "inb : %04x %02x\n", addr, val);
#endif
#ifdef USE_KQEMU
    if (env)
        env->last_io_time = cpu_get_time_fast();
#endif
    return val;
}

int cpu_inw(CPUState *env, int addr)
{
    int val;
    val = ioport_read(1, addr);
#ifdef DEBUG_IOPORT
    if (loglevel & CPU_LOG_IOPORT)
        fprintf(logfile, "inw : %04x %04x\n", addr, val);
#endif
#ifdef USE_KQEMU
    if (env)
        env->last_io_time = cpu_get_time_fast();
#endif
    return val;
}

int cpu_inl(CPUState *env, int addr)
{
    int val;
    val = ioport_read(2, addr);
#ifdef DEBUG_IOPORT
    if (loglevel & CPU_LOG_IOPORT)
        fprintf(logfile, "inl : %04x %08x\n", addr, val);
#endif
#ifdef USE_KQEMU
    if (env)
        env->last_io_time = cpu_get_time_fast();
#endif
    return val;
}

/***********************************************************/
void hw_error(const char *fmt, ...)
{
    va_list ap;
    CPUState *env;

    va_start(ap, fmt);
    fprintf(stderr, "qemu: hardware error: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    for(env = first_cpu; env != NULL; env = env->next_cpu) {
        fprintf(stderr, "CPU #%d:\n", env->cpu_index);
#ifdef TARGET_I386
        cpu_dump_state(env, stderr, fprintf, X86_DUMP_FPU);
#else
        cpu_dump_state(env, stderr, fprintf, 0);
#endif
    }
    va_end(ap);
    abort();
}

/***********************************************************/
/* keyboard/mouse */

static QEMUPutKBDEvent *qemu_put_kbd_event;
static void *qemu_put_kbd_event_opaque;
static QEMUPutMouseEntry *qemu_put_mouse_event_head;
static QEMUPutMouseEntry *qemu_put_mouse_event_current;

void qemu_add_kbd_event_handler(QEMUPutKBDEvent *func, void *opaque)
{
    qemu_put_kbd_event_opaque = opaque;
    qemu_put_kbd_event = func;
}

QEMUPutMouseEntry *qemu_add_mouse_event_handler(QEMUPutMouseEvent *func,
                                                void *opaque, int absolute,
                                                const char *name)
{
    QEMUPutMouseEntry *s, *cursor;

    s = qemu_mallocz(sizeof(QEMUPutMouseEntry));
    if (!s)
        return NULL;

    s->qemu_put_mouse_event = func;
    s->qemu_put_mouse_event_opaque = opaque;
    s->qemu_put_mouse_event_absolute = absolute;
    s->qemu_put_mouse_event_name = qemu_strdup(name);
    s->next = NULL;

    if (!qemu_put_mouse_event_head) {
        qemu_put_mouse_event_head = qemu_put_mouse_event_current = s;
        return s;
    }

    cursor = qemu_put_mouse_event_head;
    while (cursor->next != NULL)
        cursor = cursor->next;

    cursor->next = s;
    qemu_put_mouse_event_current = s;

    return s;
}

void qemu_remove_mouse_event_handler(QEMUPutMouseEntry *entry)
{
    QEMUPutMouseEntry *prev = NULL, *cursor;

    if (!qemu_put_mouse_event_head || entry == NULL)
        return;

    cursor = qemu_put_mouse_event_head;
    while (cursor != NULL && cursor != entry) {
        prev = cursor;
        cursor = cursor->next;
    }

    if (cursor == NULL) // does not exist or list empty
        return;
    else if (prev == NULL) { // entry is head
        qemu_put_mouse_event_head = cursor->next;
        if (qemu_put_mouse_event_current == entry)
            qemu_put_mouse_event_current = cursor->next;
        qemu_free(entry->qemu_put_mouse_event_name);
        qemu_free(entry);
        return;
    }

    prev->next = entry->next;

    if (qemu_put_mouse_event_current == entry)
        qemu_put_mouse_event_current = prev;

    qemu_free(entry->qemu_put_mouse_event_name);
    qemu_free(entry);
}

void kbd_put_keycode(int keycode)
{
    if (qemu_put_kbd_event) {
        qemu_put_kbd_event(qemu_put_kbd_event_opaque, keycode);
    }
}

void kbd_mouse_event(int dx, int dy, int dz, int buttons_state)
{
    QEMUPutMouseEvent *mouse_event;
    void *mouse_event_opaque;
    int width;

    if (!qemu_put_mouse_event_current) {
        return;
    }

    mouse_event =
        qemu_put_mouse_event_current->qemu_put_mouse_event;
    mouse_event_opaque =
        qemu_put_mouse_event_current->qemu_put_mouse_event_opaque;

    if (mouse_event) {
        if (graphic_rotate) {
            if (qemu_put_mouse_event_current->qemu_put_mouse_event_absolute)
                width = 0x7fff;
            else
                width = graphic_width - 1;
            mouse_event(mouse_event_opaque,
                                 width - dy, dx, dz, buttons_state);
        } else
            mouse_event(mouse_event_opaque,
                                 dx, dy, dz, buttons_state);
    }
}

int kbd_mouse_is_absolute(void)
{
    if (!qemu_put_mouse_event_current)
        return 0;

    return qemu_put_mouse_event_current->qemu_put_mouse_event_absolute;
}

void do_info_mice(void)
{
    QEMUPutMouseEntry *cursor;
    int index = 0;

    if (!qemu_put_mouse_event_head) {
        term_printf("No mouse devices connected\n");
        return;
    }

    term_printf("Mouse devices available:\n");
    cursor = qemu_put_mouse_event_head;
    while (cursor != NULL) {
        term_printf("%c Mouse #%d: %s\n",
                    (cursor == qemu_put_mouse_event_current ? '*' : ' '),
                    index, cursor->qemu_put_mouse_event_name);
        index++;
        cursor = cursor->next;
    }
}

void do_mouse_set(int index)
{
    QEMUPutMouseEntry *cursor;
    int i = 0;

    if (!qemu_put_mouse_event_head) {
        term_printf("No mouse devices connected\n");
        return;
    }

    cursor = qemu_put_mouse_event_head;
    while (cursor != NULL && index != i) {
        i++;
        cursor = cursor->next;
    }

    if (cursor != NULL)
        qemu_put_mouse_event_current = cursor;
    else
        term_printf("Mouse at given index not found\n");
}

/* compute with 96 bit intermediate result: (a*b)/c */
uint64_t muldiv64(uint64_t a, uint32_t b, uint32_t c)
{
    union {
        uint64_t ll;
        struct {
#ifdef WORDS_BIGENDIAN
            uint32_t high, low;
#else
            uint32_t low, high;
#endif
        } l;
    } u, res;
    uint64_t rl, rh;

    u.ll = a;
    rl = (uint64_t)u.l.low * (uint64_t)b;
    rh = (uint64_t)u.l.high * (uint64_t)b;
    rh += (rl >> 32);
    res.l.high = rh / c;
    res.l.low = (((rh % c) << 32) + (rl & 0xffffffff)) / c;
    return res.ll;
}

/***********************************************************/
/* real time host monotonic timer */

#define QEMU_TIMER_BASE 1000000000LL

#ifdef WIN32

static int64_t clock_freq;

static void init_get_clock(void)
{
    LARGE_INTEGER freq;
    int ret;
    ret = QueryPerformanceFrequency(&freq);
    if (ret == 0) {
        fprintf(stderr, "Could not calibrate ticks\n");
        exit(1);
    }
    clock_freq = freq.QuadPart;
}

static int64_t get_clock(void)
{
    LARGE_INTEGER ti;
    QueryPerformanceCounter(&ti);
    return muldiv64(ti.QuadPart, QEMU_TIMER_BASE, clock_freq);
}

#else

static int use_rt_clock;

static void init_get_clock(void)
{
    use_rt_clock = 0;
#if defined(__linux__)
    {
        struct timespec ts;
        if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
            use_rt_clock = 1;
        }
    }
#endif
}

static int64_t get_clock(void)
{
#if defined(__linux__)
    if (use_rt_clock) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return ts.tv_sec * 1000000000LL + ts.tv_nsec;
    } else
#endif
    {
        /* XXX: using gettimeofday leads to problems if the date
           changes, so it should be avoided. */
        struct timeval tv;
        gettimeofday(&tv, NULL);
        return tv.tv_sec * 1000000000LL + (tv.tv_usec * 1000);
    }
}
#endif

/* Return the virtual CPU time, based on the instruction counter.  */
static int64_t cpu_get_icount(void)
{
    int64_t icount;
    CPUState *env = cpu_single_env;;
    icount = qemu_icount;
    if (env) {
        if (!can_do_io(env))
            fprintf(stderr, "Bad clock read\n");
        icount -= (env->icount_decr.u16.low + env->icount_extra);
    }
    return qemu_icount_bias + (icount << icount_time_shift);
}

/***********************************************************/
/* guest cycle counter */

static int64_t cpu_ticks_prev;
static int64_t cpu_ticks_offset;
static int64_t cpu_clock_offset;
static int cpu_ticks_enabled;

/* return the host CPU cycle counter and handle stop/restart */
int64_t cpu_get_ticks(void)
{
    if (use_icount) {
        return cpu_get_icount();
    }
    if (!cpu_ticks_enabled) {
        return cpu_ticks_offset;
    } else {
        int64_t ticks;
        ticks = cpu_get_real_ticks();
        if (cpu_ticks_prev > ticks) {
            /* Note: non increasing ticks may happen if the host uses
               software suspend */
            cpu_ticks_offset += cpu_ticks_prev - ticks;
        }
        cpu_ticks_prev = ticks;
        return ticks + cpu_ticks_offset;
    }
}

/* return the host CPU monotonic timer and handle stop/restart */
static int64_t cpu_get_clock(void)
{
    int64_t ti;
    if (!cpu_ticks_enabled) {
        return cpu_clock_offset;
    } else {
        ti = get_clock();
        return ti + cpu_clock_offset;
    }
}

/* enable cpu_get_ticks() */
void cpu_enable_ticks(void)
{
    if (!cpu_ticks_enabled) {
        cpu_ticks_offset -= cpu_get_real_ticks();
        cpu_clock_offset -= get_clock();
        cpu_ticks_enabled = 1;
    }
}

/* disable cpu_get_ticks() : the clock is stopped. You must not call
   cpu_get_ticks() after that.  */
void cpu_disable_ticks(void)
{
    if (cpu_ticks_enabled) {
        cpu_ticks_offset = cpu_get_ticks();
        cpu_clock_offset = cpu_get_clock();
        cpu_ticks_enabled = 0;
    }
}

/***********************************************************/
/* timers */

#define QEMU_TIMER_REALTIME 0
#define QEMU_TIMER_VIRTUAL  1

struct QEMUClock {
    int type;
    /* XXX: add frequency */
};

struct QEMUTimer {
    QEMUClock *clock;
    int64_t expire_time;
    QEMUTimerCB *cb;
    void *opaque;
    struct QEMUTimer *next;
};

struct qemu_alarm_timer {
    char const *name;
    unsigned int flags;

    int (*start)(struct qemu_alarm_timer *t);
    void (*stop)(struct qemu_alarm_timer *t);
    void (*rearm)(struct qemu_alarm_timer *t);
    void *priv;
};

#define ALARM_FLAG_DYNTICKS  0x1
#define ALARM_FLAG_EXPIRED   0x2

static inline int alarm_has_dynticks(struct qemu_alarm_timer *t)
{
    return t->flags & ALARM_FLAG_DYNTICKS;
}

static void qemu_rearm_alarm_timer(struct qemu_alarm_timer *t)
{
    if (!alarm_has_dynticks(t))
        return;

    t->rearm(t);
}

/* TODO: MIN_TIMER_REARM_US should be optimized */
#define MIN_TIMER_REARM_US 250

static struct qemu_alarm_timer *alarm_timer;

#ifdef _WIN32

struct qemu_alarm_win32 {
    MMRESULT timerId;
    HANDLE host_alarm;
    unsigned int period;
} alarm_win32_data = {0, NULL, -1};

static int win32_start_timer(struct qemu_alarm_timer *t);
static void win32_stop_timer(struct qemu_alarm_timer *t);
static void win32_rearm_timer(struct qemu_alarm_timer *t);

#else

static int unix_start_timer(struct qemu_alarm_timer *t);
static void unix_stop_timer(struct qemu_alarm_timer *t);

#ifdef __linux__

static int dynticks_start_timer(struct qemu_alarm_timer *t);
static void dynticks_stop_timer(struct qemu_alarm_timer *t);
static void dynticks_rearm_timer(struct qemu_alarm_timer *t);

static int hpet_start_timer(struct qemu_alarm_timer *t);
static void hpet_stop_timer(struct qemu_alarm_timer *t);

static int rtc_start_timer(struct qemu_alarm_timer *t);
static void rtc_stop_timer(struct qemu_alarm_timer *t);

#endif /* __linux__ */

#endif /* _WIN32 */

/* Correlation between real and virtual time is always going to be
   fairly approximate, so ignore small variation.
   When the guest is idle real and virtual time will be aligned in
   the IO wait loop.  */
#define ICOUNT_WOBBLE (QEMU_TIMER_BASE / 10)

static void icount_adjust(void)
{
    int64_t cur_time;
    int64_t cur_icount;
    int64_t delta;
    static int64_t last_delta;
    /* If the VM is not running, then do nothing.  */
    if (!vm_running)
        return;

    cur_time = cpu_get_clock();
    cur_icount = qemu_get_clock(vm_clock);
    delta = cur_icount - cur_time;
    /* FIXME: This is a very crude algorithm, somewhat prone to oscillation.  */
    if (delta > 0
        && last_delta + ICOUNT_WOBBLE < delta * 2
        && icount_time_shift > 0) {
        /* The guest is getting too far ahead.  Slow time down.  */
        icount_time_shift--;
    }
    if (delta < 0
        && last_delta - ICOUNT_WOBBLE > delta * 2
        && icount_time_shift < MAX_ICOUNT_SHIFT) {
        /* The guest is getting too far behind.  Speed time up.  */
        icount_time_shift++;
    }
    last_delta = delta;
    qemu_icount_bias = cur_icount - (qemu_icount << icount_time_shift);
}

static void icount_adjust_rt(void * opaque)
{
    qemu_mod_timer(icount_rt_timer,
                   qemu_get_clock(rt_clock) + 1000);
    icount_adjust();
}

static void icount_adjust_vm(void * opaque)
{
    qemu_mod_timer(icount_vm_timer,
                   qemu_get_clock(vm_clock) + QEMU_TIMER_BASE / 10);
    icount_adjust();
}

static void init_icount_adjust(void)
{
    /* Have both realtime and virtual time triggers for speed adjustment.
       The realtime trigger catches emulated time passing too slowly,
       the virtual time trigger catches emulated time passing too fast.
       Realtime triggers occur even when idle, so use them less frequently
       than VM triggers.  */
    icount_rt_timer = qemu_new_timer(rt_clock, icount_adjust_rt, NULL);
    qemu_mod_timer(icount_rt_timer,
                   qemu_get_clock(rt_clock) + 1000);
    icount_vm_timer = qemu_new_timer(vm_clock, icount_adjust_vm, NULL);
    qemu_mod_timer(icount_vm_timer,
                   qemu_get_clock(vm_clock) + QEMU_TIMER_BASE / 10);
}

static struct qemu_alarm_timer alarm_timers[] = {
#ifndef _WIN32
#ifdef __linux__
    {"dynticks", ALARM_FLAG_DYNTICKS, dynticks_start_timer,
     dynticks_stop_timer, dynticks_rearm_timer, NULL},
    /* HPET - if available - is preferred */
    {"hpet", 0, hpet_start_timer, hpet_stop_timer, NULL, NULL},
    /* ...otherwise try RTC */
    {"rtc", 0, rtc_start_timer, rtc_stop_timer, NULL, NULL},
#endif
    {"unix", 0, unix_start_timer, unix_stop_timer, NULL, NULL},
#else
    {"dynticks", ALARM_FLAG_DYNTICKS, win32_start_timer,
     win32_stop_timer, win32_rearm_timer, &alarm_win32_data},
    {"win32", 0, win32_start_timer,
     win32_stop_timer, NULL, &alarm_win32_data},
#endif
    {NULL, }
};

static void show_available_alarms(void)
{
    int i;

    printf("Available alarm timers, in order of precedence:\n");
    for (i = 0; alarm_timers[i].name; i++)
        printf("%s\n", alarm_timers[i].name);
}

static void configure_alarms(char const *opt)
{
    int i;
    int cur = 0;
    int count = (sizeof(alarm_timers) / sizeof(*alarm_timers)) - 1;
    char *arg;
    char *name;
    struct qemu_alarm_timer tmp;

    if (!strcmp(opt, "?")) {
        show_available_alarms();
        exit(0);
    }

    arg = strdup(opt);

    /* Reorder the array */
    name = strtok(arg, ",");
    while (name) {
        for (i = 0; i < count && alarm_timers[i].name; i++) {
            if (!strcmp(alarm_timers[i].name, name))
                break;
        }

        if (i == count) {
            fprintf(stderr, "Unknown clock %s\n", name);
            goto next;
        }

        if (i < cur)
            /* Ignore */
            goto next;

	/* Swap */
        tmp = alarm_timers[i];
        alarm_timers[i] = alarm_timers[cur];
        alarm_timers[cur] = tmp;

        cur++;
next:
        name = strtok(NULL, ",");
    }

    free(arg);

    if (cur) {
        /* Disable remaining timers */
        for (i = cur; i < count; i++)
            alarm_timers[i].name = NULL;
    } else {
        show_available_alarms();
        exit(1);
    }
}

QEMUClock *rt_clock;
QEMUClock *vm_clock;

static QEMUTimer *active_timers[2];

static QEMUClock *qemu_new_clock(int type)
{
    QEMUClock *clock;
    clock = qemu_mallocz(sizeof(QEMUClock));
    if (!clock)
        return NULL;
    clock->type = type;
    return clock;
}

QEMUTimer *qemu_new_timer(QEMUClock *clock, QEMUTimerCB *cb, void *opaque)
{
    QEMUTimer *ts;

    ts = qemu_mallocz(sizeof(QEMUTimer));
    ts->clock = clock;
    ts->cb = cb;
    ts->opaque = opaque;
    return ts;
}

void qemu_free_timer(QEMUTimer *ts)
{
    qemu_free(ts);
}

/* stop a timer, but do not dealloc it */
void qemu_del_timer(QEMUTimer *ts)
{
    QEMUTimer **pt, *t;

    /* NOTE: this code must be signal safe because
       qemu_timer_expired() can be called from a signal. */
    pt = &active_timers[ts->clock->type];
    for(;;) {
        t = *pt;
        if (!t)
            break;
        if (t == ts) {
            *pt = t->next;
            break;
        }
        pt = &t->next;
    }
}

/* modify the current timer so that it will be fired when current_time
   >= expire_time. The corresponding callback will be called. */
void qemu_mod_timer(QEMUTimer *ts, int64_t expire_time)
{
    QEMUTimer **pt, *t;

    qemu_del_timer(ts);

    /* add the timer in the sorted list */
    /* NOTE: this code must be signal safe because
       qemu_timer_expired() can be called from a signal. */
    pt = &active_timers[ts->clock->type];
    for(;;) {
        t = *pt;
        if (!t)
            break;
        if (t->expire_time > expire_time)
            break;
        pt = &t->next;
    }
    ts->expire_time = expire_time;
    ts->next = *pt;
    *pt = ts;

    /* Rearm if necessary  */
    if (pt == &active_timers[ts->clock->type]) {
        if ((alarm_timer->flags & ALARM_FLAG_EXPIRED) == 0) {
            qemu_rearm_alarm_timer(alarm_timer);
        }
        /* Interrupt execution to force deadline recalculation.  */
        if (use_icount && cpu_single_env) {
            cpu_interrupt(cpu_single_env, CPU_INTERRUPT_EXIT);
        }
    }
}

int qemu_timer_pending(QEMUTimer *ts)
{
    QEMUTimer *t;
    for(t = active_timers[ts->clock->type]; t != NULL; t = t->next) {
        if (t == ts)
            return 1;
    }
    return 0;
}

static inline int qemu_timer_expired(QEMUTimer *timer_head, int64_t current_time)
{
    if (!timer_head)
        return 0;
    return (timer_head->expire_time <= current_time);
}

static void qemu_run_timers(QEMUTimer **ptimer_head, int64_t current_time)
{
    QEMUTimer *ts;

    for(;;) {
        ts = *ptimer_head;
        if (!ts || ts->expire_time > current_time)
            break;
        /* remove timer from the list before calling the callback */
        *ptimer_head = ts->next;
        ts->next = NULL;

        /* run the callback (the timer list can be modified) */
        ts->cb(ts->opaque);
    }
}

int64_t qemu_get_clock(QEMUClock *clock)
{
    switch(clock->type) {
    case QEMU_TIMER_REALTIME:
        return get_clock() / 1000000;
    default:
    case QEMU_TIMER_VIRTUAL:
        if (use_icount) {
            return cpu_get_icount();
        } else {
            return cpu_get_clock();
        }
    }
}

static void init_timers(void)
{
    init_get_clock();
    ticks_per_sec = QEMU_TIMER_BASE;
    rt_clock = qemu_new_clock(QEMU_TIMER_REALTIME);
    vm_clock = qemu_new_clock(QEMU_TIMER_VIRTUAL);
}

/* save a timer */
void qemu_put_timer(QEMUFile *f, QEMUTimer *ts)
{
    uint64_t expire_time;

    if (qemu_timer_pending(ts)) {
        expire_time = ts->expire_time;
    } else {
        expire_time = -1;
    }
    qemu_put_be64(f, expire_time);
}

void qemu_get_timer(QEMUFile *f, QEMUTimer *ts)
{
    uint64_t expire_time;

    expire_time = qemu_get_be64(f);
    if (expire_time != -1) {
        qemu_mod_timer(ts, expire_time);
    } else {
        qemu_del_timer(ts);
    }
}

static void timer_save(QEMUFile *f, void *opaque)
{
    if (cpu_ticks_enabled) {
        hw_error("cannot save state if virtual timers are running");
    }
    qemu_put_be64(f, cpu_ticks_offset);
    qemu_put_be64(f, ticks_per_sec);
    qemu_put_be64(f, cpu_clock_offset);
}

static int timer_load(QEMUFile *f, void *opaque, int version_id)
{
    if (version_id != 1 && version_id != 2)
        return -EINVAL;
    if (cpu_ticks_enabled) {
        return -EINVAL;
    }
    cpu_ticks_offset=qemu_get_be64(f);
    ticks_per_sec=qemu_get_be64(f);
    if (version_id == 2) {
        cpu_clock_offset=qemu_get_be64(f);
    }
    return 0;
}

#ifdef _WIN32
void CALLBACK host_alarm_handler(UINT uTimerID, UINT uMsg,
                                 DWORD_PTR dwUser, DWORD_PTR dw1, DWORD_PTR dw2)
#else
static void host_alarm_handler(int host_signum)
#endif
{
#if 0
#define DISP_FREQ 1000
    {
        static int64_t delta_min = INT64_MAX;
        static int64_t delta_max, delta_cum, last_clock, delta, ti;
        static int count;
        ti = qemu_get_clock(vm_clock);
        if (last_clock != 0) {
            delta = ti - last_clock;
            if (delta < delta_min)
                delta_min = delta;
            if (delta > delta_max)
                delta_max = delta;
            delta_cum += delta;
            if (++count == DISP_FREQ) {
                printf("timer: min=%" PRId64 " us max=%" PRId64 " us avg=%" PRId64 " us avg_freq=%0.3f Hz\n",
                       muldiv64(delta_min, 1000000, ticks_per_sec),
                       muldiv64(delta_max, 1000000, ticks_per_sec),
                       muldiv64(delta_cum, 1000000 / DISP_FREQ, ticks_per_sec),
                       (double)ticks_per_sec / ((double)delta_cum / DISP_FREQ));
                count = 0;
                delta_min = INT64_MAX;
                delta_max = 0;
                delta_cum = 0;
            }
        }
        last_clock = ti;
    }
#endif
    if (alarm_has_dynticks(alarm_timer) ||
        (!use_icount &&
            qemu_timer_expired(active_timers[QEMU_TIMER_VIRTUAL],
                               qemu_get_clock(vm_clock))) ||
        qemu_timer_expired(active_timers[QEMU_TIMER_REALTIME],
                           qemu_get_clock(rt_clock))) {
#ifdef _WIN32
        struct qemu_alarm_win32 *data = ((struct qemu_alarm_timer*)dwUser)->priv;
        SetEvent(data->host_alarm);
#endif
        CPUState *env = next_cpu;

        alarm_timer->flags |= ALARM_FLAG_EXPIRED;

        if (env) {
            /* stop the currently executing cpu because a timer occured */
            cpu_interrupt(env, CPU_INTERRUPT_EXIT);
#ifdef USE_KQEMU
            if (env->kqemu_enabled) {
                kqemu_cpu_interrupt(env);
            }
#endif
        }
        event_pending = 1;
    }
}

static int64_t qemu_next_deadline(void)
{
    int64_t delta;

    if (active_timers[QEMU_TIMER_VIRTUAL]) {
        delta = active_timers[QEMU_TIMER_VIRTUAL]->expire_time -
                     qemu_get_clock(vm_clock);
    } else {
        /* To avoid problems with overflow limit this to 2^32.  */
        delta = INT32_MAX;
    }

    if (delta < 0)
        delta = 0;

    return delta;
}

#if defined(__linux__) || defined(_WIN32)
static uint64_t qemu_next_deadline_dyntick(void)
{
    int64_t delta;
    int64_t rtdelta;

    if (use_icount)
        delta = INT32_MAX;
    else
        delta = (qemu_next_deadline() + 999) / 1000;

    if (active_timers[QEMU_TIMER_REALTIME]) {
        rtdelta = (active_timers[QEMU_TIMER_REALTIME]->expire_time -
                 qemu_get_clock(rt_clock))*1000;
        if (rtdelta < delta)
            delta = rtdelta;
    }

    if (delta < MIN_TIMER_REARM_US)
        delta = MIN_TIMER_REARM_US;

    return delta;
}
#endif

#ifndef _WIN32

#if defined(__linux__)

#define RTC_FREQ 1024

static void enable_sigio_timer(int fd)
{
    struct sigaction act;

    /* timer signal */
    sigfillset(&act.sa_mask);
    act.sa_flags = 0;
    act.sa_handler = host_alarm_handler;

    sigaction(SIGIO, &act, NULL);
    fcntl(fd, F_SETFL, O_ASYNC);
    fcntl(fd, F_SETOWN, getpid());
}

static int hpet_start_timer(struct qemu_alarm_timer *t)
{
    struct hpet_info info;
    int r, fd;

    fd = open("/dev/hpet", O_RDONLY);
    if (fd < 0)
        return -1;

    /* Set frequency */
    r = ioctl(fd, HPET_IRQFREQ, RTC_FREQ);
    if (r < 0) {
        fprintf(stderr, "Could not configure '/dev/hpet' to have a 1024Hz timer. This is not a fatal\n"
                "error, but for better emulation accuracy type:\n"
                "'echo 1024 > /proc/sys/dev/hpet/max-user-freq' as root.\n");
        goto fail;
    }

    /* Check capabilities */
    r = ioctl(fd, HPET_INFO, &info);
    if (r < 0)
        goto fail;

    /* Enable periodic mode */
    r = ioctl(fd, HPET_EPI, 0);
    if (info.hi_flags && (r < 0))
        goto fail;

    /* Enable interrupt */
    r = ioctl(fd, HPET_IE_ON, 0);
    if (r < 0)
        goto fail;

    enable_sigio_timer(fd);
    t->priv = (void *)(long)fd;

    return 0;
fail:
    close(fd);
    return -1;
}

static void hpet_stop_timer(struct qemu_alarm_timer *t)
{
    int fd = (long)t->priv;

    close(fd);
}

static int rtc_start_timer(struct qemu_alarm_timer *t)
{
    int rtc_fd;
    unsigned long current_rtc_freq = 0;

    TFR(rtc_fd = open("/dev/rtc", O_RDONLY));
    if (rtc_fd < 0)
        return -1;
    ioctl(rtc_fd, RTC_IRQP_READ, &current_rtc_freq);
    if (current_rtc_freq != RTC_FREQ &&
        ioctl(rtc_fd, RTC_IRQP_SET, RTC_FREQ) < 0) {
        fprintf(stderr, "Could not configure '/dev/rtc' to have a 1024 Hz timer. This is not a fatal\n"
                "error, but for better emulation accuracy either use a 2.6 host Linux kernel or\n"
                "type 'echo 1024 > /proc/sys/dev/rtc/max-user-freq' as root.\n");
        goto fail;
    }
    if (ioctl(rtc_fd, RTC_PIE_ON, 0) < 0) {
    fail:
        close(rtc_fd);
        return -1;
    }

    enable_sigio_timer(rtc_fd);

    t->priv = (void *)(long)rtc_fd;

    return 0;
}

static void rtc_stop_timer(struct qemu_alarm_timer *t)
{
    int rtc_fd = (long)t->priv;

    close(rtc_fd);
}

static int dynticks_start_timer(struct qemu_alarm_timer *t)
{
    struct sigevent ev;
    timer_t host_timer;
    struct sigaction act;

    sigfillset(&act.sa_mask);
    act.sa_flags = 0;
    act.sa_handler = host_alarm_handler;

    sigaction(SIGALRM, &act, NULL);

    ev.sigev_value.sival_int = 0;
    ev.sigev_notify = SIGEV_SIGNAL;
    ev.sigev_signo = SIGALRM;

    if (timer_create(CLOCK_REALTIME, &ev, &host_timer)) {
        perror("timer_create");

        /* disable dynticks */
        fprintf(stderr, "Dynamic Ticks disabled\n");

        return -1;
    }

    t->priv = (void *)host_timer;

    return 0;
}

static void dynticks_stop_timer(struct qemu_alarm_timer *t)
{
    timer_t host_timer = (timer_t)t->priv;

    timer_delete(host_timer);
}

static void dynticks_rearm_timer(struct qemu_alarm_timer *t)
{
    timer_t host_timer = (timer_t)t->priv;
    struct itimerspec timeout;
    int64_t nearest_delta_us = INT64_MAX;
    int64_t current_us;

    if (!active_timers[QEMU_TIMER_REALTIME] &&
                !active_timers[QEMU_TIMER_VIRTUAL])
        return;

    nearest_delta_us = qemu_next_deadline_dyntick();

    /* check whether a timer is already running */
    if (timer_gettime(host_timer, &timeout)) {
        perror("gettime");
        fprintf(stderr, "Internal timer error: aborting\n");
        exit(1);
    }
    current_us = timeout.it_value.tv_sec * 1000000 + timeout.it_value.tv_nsec/1000;
    if (current_us && current_us <= nearest_delta_us)
        return;

    timeout.it_interval.tv_sec = 0;
    timeout.it_interval.tv_nsec = 0; /* 0 for one-shot timer */
    timeout.it_value.tv_sec =  nearest_delta_us / 1000000;
    timeout.it_value.tv_nsec = (nearest_delta_us % 1000000) * 1000;
    if (timer_settime(host_timer, 0 /* RELATIVE */, &timeout, NULL)) {
        perror("settime");
        fprintf(stderr, "Internal timer error: aborting\n");
        exit(1);
    }
}

#endif /* defined(__linux__) */

static int unix_start_timer(struct qemu_alarm_timer *t)
{
    struct sigaction act;
    struct itimerval itv;
    int err;

    /* timer signal */
    sigfillset(&act.sa_mask);
    act.sa_flags = 0;
    act.sa_handler = host_alarm_handler;

    sigaction(SIGALRM, &act, NULL);

    itv.it_interval.tv_sec = 0;
    /* for i386 kernel 2.6 to get 1 ms */
    itv.it_interval.tv_usec = 999;
    itv.it_value.tv_sec = 0;
    itv.it_value.tv_usec = 10 * 1000;

    err = setitimer(ITIMER_REAL, &itv, NULL);
    if (err)
        return -1;

    return 0;
}

static void unix_stop_timer(struct qemu_alarm_timer *t)
{
    struct itimerval itv;

    memset(&itv, 0, sizeof(itv));
    setitimer(ITIMER_REAL, &itv, NULL);
}

#endif /* !defined(_WIN32) */

#ifdef _WIN32

static int win32_start_timer(struct qemu_alarm_timer *t)
{
    TIMECAPS tc;
    struct qemu_alarm_win32 *data = t->priv;
    UINT flags;

    data->host_alarm = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!data->host_alarm) {
        perror("Failed CreateEvent");
        return -1;
    }

    memset(&tc, 0, sizeof(tc));
    timeGetDevCaps(&tc, sizeof(tc));

    if (data->period < tc.wPeriodMin)
        data->period = tc.wPeriodMin;

    timeBeginPeriod(data->period);

    flags = TIME_CALLBACK_FUNCTION;
    if (alarm_has_dynticks(t))
        flags |= TIME_ONESHOT;
    else
        flags |= TIME_PERIODIC;

    data->timerId = timeSetEvent(1,         // interval (ms)
                        data->period,       // resolution
                        host_alarm_handler, // function
                        (DWORD)t,           // parameter
                        flags);

    if (!data->timerId) {
        perror("Failed to initialize win32 alarm timer");

        timeEndPeriod(data->period);
        CloseHandle(data->host_alarm);
        return -1;
    }

    qemu_add_wait_object(data->host_alarm, NULL, NULL);

    return 0;
}

static void win32_stop_timer(struct qemu_alarm_timer *t)
{
    struct qemu_alarm_win32 *data = t->priv;

    timeKillEvent(data->timerId);
    timeEndPeriod(data->period);

    CloseHandle(data->host_alarm);
}

static void win32_rearm_timer(struct qemu_alarm_timer *t)
{
    struct qemu_alarm_win32 *data = t->priv;
    uint64_t nearest_delta_us;

    if (!active_timers[QEMU_TIMER_REALTIME] &&
                !active_timers[QEMU_TIMER_VIRTUAL])
        return;

    nearest_delta_us = qemu_next_deadline_dyntick();
    nearest_delta_us /= 1000;

    timeKillEvent(data->timerId);

    data->timerId = timeSetEvent(1,
                        data->period,
                        host_alarm_handler,
                        (DWORD)t,
                        TIME_ONESHOT | TIME_PERIODIC);

    if (!data->timerId) {
        perror("Failed to re-arm win32 alarm timer");

        timeEndPeriod(data->period);
        CloseHandle(data->host_alarm);
        exit(1);
    }
}

#endif /* _WIN32 */

static void init_timer_alarm(void)
{
    struct qemu_alarm_timer *t = NULL;
    int i, err = -1;

    for (i = 0; alarm_timers[i].name; i++) {
        t = &alarm_timers[i];

        err = t->start(t);
        if (!err)
            break;
    }

    if (err) {
        fprintf(stderr, "Unable to find any suitable alarm timer.\n");
        fprintf(stderr, "Terminating\n");
        exit(1);
    }

    alarm_timer = t;
}

static void quit_timers(void)
{
    alarm_timer->stop(alarm_timer);
    alarm_timer = NULL;
}

/***********************************************************/
/* host time/date access */
void qemu_get_timedate(struct tm *tm, int offset)
{
    time_t ti;
    struct tm *ret;

    time(&ti);
    ti += offset;
    if (rtc_date_offset == -1) {
        if (rtc_utc)
            ret = gmtime(&ti);
        else
            ret = localtime(&ti);
    } else {
        ti -= rtc_date_offset;
        ret = gmtime(&ti);
    }

    memcpy(tm, ret, sizeof(struct tm));
}

int qemu_timedate_diff(struct tm *tm)
{
    time_t seconds;

    if (rtc_date_offset == -1)
        if (rtc_utc)
            seconds = mktimegm(tm);
        else
            seconds = mktime(tm);
    else
        seconds = mktimegm(tm) + rtc_date_offset;

    return seconds - time(NULL);
}

/***********************************************************/
/* character device */

static void qemu_chr_event(CharDriverState *s, int event)
{
    if (!s->chr_event)
        return;
    s->chr_event(s->handler_opaque, event);
}

static void qemu_chr_reset_bh(void *opaque)
{
    CharDriverState *s = opaque;
    qemu_chr_event(s, CHR_EVENT_RESET);
    qemu_bh_delete(s->bh);
    s->bh = NULL;
}

void qemu_chr_reset(CharDriverState *s)
{
    if (s->bh == NULL) {
	s->bh = qemu_bh_new(qemu_chr_reset_bh, s);
	qemu_bh_schedule(s->bh);
    }
}

int qemu_chr_write(CharDriverState *s, const uint8_t *buf, int len)
{
    return s->chr_write(s, buf, len);
}

int qemu_chr_ioctl(CharDriverState *s, int cmd, void *arg)
{
    if (!s->chr_ioctl)
        return -ENOTSUP;
    return s->chr_ioctl(s, cmd, arg);
}

int qemu_chr_can_read(CharDriverState *s)
{
    if (!s->chr_can_read)
        return 0;
    return s->chr_can_read(s->handler_opaque);
}

void qemu_chr_read(CharDriverState *s, uint8_t *buf, int len)
{
    s->chr_read(s->handler_opaque, buf, len);
}

void qemu_chr_accept_input(CharDriverState *s)
{
    if (s->chr_accept_input)
        s->chr_accept_input(s);
}

void qemu_chr_printf(CharDriverState *s, const char *fmt, ...)
{
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    qemu_chr_write(s, (uint8_t *)buf, strlen(buf));
    va_end(ap);
}

void qemu_chr_send_event(CharDriverState *s, int event)
{
    if (s->chr_send_event)
        s->chr_send_event(s, event);
}

void qemu_chr_add_handlers(CharDriverState *s,
                           IOCanRWHandler *fd_can_read,
                           IOReadHandler *fd_read,
                           IOEventHandler *fd_event,
                           void *opaque)
{
    s->chr_can_read = fd_can_read;
    s->chr_read = fd_read;
    s->chr_event = fd_event;
    s->handler_opaque = opaque;
    if (s->chr_update_read_handler)
        s->chr_update_read_handler(s);
}

static int null_chr_write(CharDriverState *chr, const uint8_t *buf, int len)
{
    return len;
}

static CharDriverState *qemu_chr_open_null(void)
{
    CharDriverState *chr;

    chr = qemu_mallocz(sizeof(CharDriverState));
    if (!chr)
        return NULL;
    chr->chr_write = null_chr_write;
    return chr;
}

/* MUX driver for serial I/O splitting */
static int term_timestamps;
static int64_t term_timestamps_start;
#define MAX_MUX 4
#define MUX_BUFFER_SIZE 32	/* Must be a power of 2.  */
#define MUX_BUFFER_MASK (MUX_BUFFER_SIZE - 1)
typedef struct {
    IOCanRWHandler *chr_can_read[MAX_MUX];
    IOReadHandler *chr_read[MAX_MUX];
    IOEventHandler *chr_event[MAX_MUX];
    void *ext_opaque[MAX_MUX];
    CharDriverState *drv;
    unsigned char buffer[MUX_BUFFER_SIZE];
    int prod;
    int cons;
    int mux_cnt;
    int term_got_escape;
    int max_size;
} MuxDriver;


static int mux_chr_write(CharDriverState *chr, const uint8_t *buf, int len)
{
    MuxDriver *d = chr->opaque;
    int ret;
    if (!term_timestamps) {
        ret = d->drv->chr_write(d->drv, buf, len);
    } else {
        int i;

        ret = 0;
        for(i = 0; i < len; i++) {
            ret += d->drv->chr_write(d->drv, buf+i, 1);
            if (buf[i] == '\n') {
                char buf1[64];
                int64_t ti;
                int secs;

                ti = get_clock();
                if (term_timestamps_start == -1)
                    term_timestamps_start = ti;
                ti -= term_timestamps_start;
                secs = ti / 1000000000;
                snprintf(buf1, sizeof(buf1),
                         "[%02d:%02d:%02d.%03d] ",
                         secs / 3600,
                         (secs / 60) % 60,
                         secs % 60,
                         (int)((ti / 1000000) % 1000));
                d->drv->chr_write(d->drv, (uint8_t *)buf1, strlen(buf1));
            }
        }
    }
    return ret;
}

static const char * const mux_help[] = {
    "% h    print this help\n\r",
    "% x    exit emulator\n\r",
    "% s    save disk data back to file (if -snapshot)\n\r",
    "% t    toggle console timestamps\n\r"
    "% b    send break (magic sysrq)\n\r",
    "% c    switch between console and monitor\n\r",
    "% %  sends %\n\r",
    NULL
};

static int term_escape_char = 0x01; /* ctrl-a is used for escape */
static void mux_print_help(CharDriverState *chr)
{
    int i, j;
    char ebuf[15] = "Escape-Char";
    char cbuf[50] = "\n\r";

    if (term_escape_char > 0 && term_escape_char < 26) {
        snprintf(cbuf, sizeof(cbuf), "\n\r");
        snprintf(ebuf, sizeof(ebuf), "C-%c", term_escape_char - 1 + 'a');
    } else {
        snprintf(cbuf, sizeof(cbuf),
                 "\n\rEscape-Char set to Ascii: 0x%02x\n\r\n\r",
                 term_escape_char);
    }
    chr->chr_write(chr, (uint8_t *)cbuf, strlen(cbuf));
    for (i = 0; mux_help[i] != NULL; i++) {
        for (j=0; mux_help[i][j] != '\0'; j++) {
            if (mux_help[i][j] == '%')
                chr->chr_write(chr, (uint8_t *)ebuf, strlen(ebuf));
            else
                chr->chr_write(chr, (uint8_t *)&mux_help[i][j], 1);
        }
    }
}

static int mux_proc_byte(CharDriverState *chr, MuxDriver *d, int ch)
{
    if (d->term_got_escape) {
        d->term_got_escape = 0;
        if (ch == term_escape_char)
            goto send_char;
        switch(ch) {
        case '?':
        case 'h':
            mux_print_help(chr);
            break;
        case 'x':
            {
                 const char *term =  "QEMU: Terminated\n\r";
                 chr->chr_write(chr,(uint8_t *)term,strlen(term));
                 exit(0);
                 break;
            }
        case 's':
            {
                int i;
                for (i = 0; i < nb_drives; i++) {
                        bdrv_commit(drives_table[i].bdrv);
                }
            }
            break;
        case 'b':
            qemu_chr_event(chr, CHR_EVENT_BREAK);
            break;
        case 'c':
            /* Switch to the next registered device */
            chr->focus++;
            if (chr->focus >= d->mux_cnt)
                chr->focus = 0;
            break;
       case 't':
           term_timestamps = !term_timestamps;
           term_timestamps_start = -1;
           break;
        }
    } else if (ch == term_escape_char) {
        d->term_got_escape = 1;
    } else {
    send_char:
        return 1;
    }
    return 0;
}

static void mux_chr_accept_input(CharDriverState *chr)
{
    int m = chr->focus;
    MuxDriver *d = chr->opaque;

    while (d->prod != d->cons &&
           d->chr_can_read[m] &&
           d->chr_can_read[m](d->ext_opaque[m])) {
        d->chr_read[m](d->ext_opaque[m],
                       &d->buffer[d->cons++ & MUX_BUFFER_MASK], 1);
    }
}

static int mux_chr_can_read(void *opaque)
{
    CharDriverState *chr = opaque;
    MuxDriver *d = chr->opaque;

    if ((d->prod - d->cons) < MUX_BUFFER_SIZE)
        return 1;
    if (d->chr_can_read[chr->focus])
        return d->chr_can_read[chr->focus](d->ext_opaque[chr->focus]);
    return 0;
}

static void mux_chr_read(void *opaque, const uint8_t *buf, int size)
{
    CharDriverState *chr = opaque;
    MuxDriver *d = chr->opaque;
    int m = chr->focus;
    int i;

    mux_chr_accept_input (opaque);

    for(i = 0; i < size; i++)
        if (mux_proc_byte(chr, d, buf[i])) {
            if (d->prod == d->cons &&
                d->chr_can_read[m] &&
                d->chr_can_read[m](d->ext_opaque[m]))
                d->chr_read[m](d->ext_opaque[m], &buf[i], 1);
            else
                d->buffer[d->prod++ & MUX_BUFFER_MASK] = buf[i];
        }
}

static void mux_chr_event(void *opaque, int event)
{
    CharDriverState *chr = opaque;
    MuxDriver *d = chr->opaque;
    int i;

    /* Send the event to all registered listeners */
    for (i = 0; i < d->mux_cnt; i++)
        if (d->chr_event[i])
            d->chr_event[i](d->ext_opaque[i], event);
}

static void mux_chr_update_read_handler(CharDriverState *chr)
{
    MuxDriver *d = chr->opaque;

    if (d->mux_cnt >= MAX_MUX) {
        fprintf(stderr, "Cannot add I/O handlers, MUX array is full\n");
        return;
    }
    d->ext_opaque[d->mux_cnt] = chr->handler_opaque;
    d->chr_can_read[d->mux_cnt] = chr->chr_can_read;
    d->chr_read[d->mux_cnt] = chr->chr_read;
    d->chr_event[d->mux_cnt] = chr->chr_event;
    /* Fix up the real driver with mux routines */
    if (d->mux_cnt == 0) {
        qemu_chr_add_handlers(d->drv, mux_chr_can_read, mux_chr_read,
                              mux_chr_event, chr);
    }
    chr->focus = d->mux_cnt;
    d->mux_cnt++;
}

static CharDriverState *qemu_chr_open_mux(CharDriverState *drv)
{
    CharDriverState *chr;
    MuxDriver *d;

    chr = qemu_mallocz(sizeof(CharDriverState));
    if (!chr)
        return NULL;
    d = qemu_mallocz(sizeof(MuxDriver));
    if (!d) {
        free(chr);
        return NULL;
    }

    chr->opaque = d;
    d->drv = drv;
    chr->focus = -1;
    chr->chr_write = mux_chr_write;
    chr->chr_update_read_handler = mux_chr_update_read_handler;
    chr->chr_accept_input = mux_chr_accept_input;
    return chr;
}


#ifdef _WIN32

static void socket_cleanup(void)
{
    WSACleanup();
}

static int socket_init(void)
{
    WSADATA Data;
    int ret, err;

    ret = WSAStartup(MAKEWORD(2,2), &Data);
    if (ret != 0) {
        err = WSAGetLastError();
        fprintf(stderr, "WSAStartup: %d\n", err);
        return -1;
    }
    atexit(socket_cleanup);
    return 0;
}

static int send_all(int fd, const uint8_t *buf, int len1)
{
    int ret, len;

    len = len1;
    while (len > 0) {
        ret = send(fd, buf, len, 0);
        if (ret < 0) {
            int errno;
            errno = WSAGetLastError();
            if (errno != WSAEWOULDBLOCK) {
                return -1;
            }
        } else if (ret == 0) {
            break;
        } else {
            buf += ret;
            len -= ret;
        }
    }
    return len1 - len;
}

#else

static int unix_write(int fd, const uint8_t *buf, int len1)
{
    int ret, len;

    len = len1;
    while (len > 0) {
        ret = write(fd, buf, len);
        if (ret < 0) {
            if (errno != EINTR && errno != EAGAIN)
                return -1;
        } else if (ret == 0) {
            break;
        } else {
            buf += ret;
            len -= ret;
        }
    }
    return len1 - len;
}

static inline int send_all(int fd, const uint8_t *buf, int len1)
{
    return unix_write(fd, buf, len1);
}
#endif /* !_WIN32 */

#ifndef _WIN32

typedef struct {
    int fd_in, fd_out;
    int max_size;
} FDCharDriver;

#define STDIO_MAX_CLIENTS 1
static int stdio_nb_clients = 0;

static int fd_chr_write(CharDriverState *chr, const uint8_t *buf, int len)
{
    FDCharDriver *s = chr->opaque;
    return unix_write(s->fd_out, buf, len);
}

static int fd_chr_read_poll(void *opaque)
{
    CharDriverState *chr = opaque;
    FDCharDriver *s = chr->opaque;

    s->max_size = qemu_chr_can_read(chr);
    return s->max_size;
}

static void fd_chr_read(void *opaque)
{
    CharDriverState *chr = opaque;
    FDCharDriver *s = chr->opaque;
    int size, len;
    uint8_t buf[1024];

    len = sizeof(buf);
    if (len > s->max_size)
        len = s->max_size;
    if (len == 0)
        return;
    size = read(s->fd_in, buf, len);
    if (size == 0) {
        /* FD has been closed. Remove it from the active list.  */
        qemu_set_fd_handler2(s->fd_in, NULL, NULL, NULL, NULL);
        return;
    }
    if (size > 0) {
        qemu_chr_read(chr, buf, size);
    }
}

static void fd_chr_update_read_handler(CharDriverState *chr)
{
    FDCharDriver *s = chr->opaque;

    if (s->fd_in >= 0) {
        if (nographic && s->fd_in == 0) {
        } else {
            qemu_set_fd_handler2(s->fd_in, fd_chr_read_poll,
                                 fd_chr_read, NULL, chr);
        }
    }
}

static void fd_chr_close(struct CharDriverState *chr)
{
    FDCharDriver *s = chr->opaque;

    if (s->fd_in >= 0) {
        if (nographic && s->fd_in == 0) {
        } else {
            qemu_set_fd_handler2(s->fd_in, NULL, NULL, NULL, NULL);
        }
    }

    qemu_free(s);
}

/* open a character device to a unix fd */
static CharDriverState *qemu_chr_open_fd(int fd_in, int fd_out)
{
    CharDriverState *chr;
    FDCharDriver *s;

    chr = qemu_mallocz(sizeof(CharDriverState));
    if (!chr)
        return NULL;
    s = qemu_mallocz(sizeof(FDCharDriver));
    if (!s) {
        free(chr);
        return NULL;
    }
    s->fd_in = fd_in;
    s->fd_out = fd_out;
    chr->opaque = s;
    chr->chr_write = fd_chr_write;
    chr->chr_update_read_handler = fd_chr_update_read_handler;
    chr->chr_close = fd_chr_close;

    qemu_chr_reset(chr);

    return chr;
}

static CharDriverState *qemu_chr_open_file_out(const char *file_out)
{
    int fd_out;

    TFR(fd_out = open(file_out, O_WRONLY | O_TRUNC | O_CREAT | O_BINARY, 0666));
    if (fd_out < 0)
        return NULL;
    return qemu_chr_open_fd(-1, fd_out);
}

static CharDriverState *qemu_chr_open_pipe(const char *filename)
{
    int fd_in, fd_out;
    char filename_in[256], filename_out[256];

    snprintf(filename_in, 256, "%s.in", filename);
    snprintf(filename_out, 256, "%s.out", filename);
    TFR(fd_in = open(filename_in, O_RDWR | O_BINARY));
    TFR(fd_out = open(filename_out, O_RDWR | O_BINARY));
    if (fd_in < 0 || fd_out < 0) {
	if (fd_in >= 0)
	    close(fd_in);
	if (fd_out >= 0)
	    close(fd_out);
        TFR(fd_in = fd_out = open(filename, O_RDWR | O_BINARY));
        if (fd_in < 0)
            return NULL;
    }
    return qemu_chr_open_fd(fd_in, fd_out);
}


/* for STDIO, we handle the case where several clients use it
   (nographic mode) */

#define TERM_FIFO_MAX_SIZE 1

static uint8_t term_fifo[TERM_FIFO_MAX_SIZE];
static int term_fifo_size;

static int stdio_read_poll(void *opaque)
{
    CharDriverState *chr = opaque;

    /* try to flush the queue if needed */
    if (term_fifo_size != 0 && qemu_chr_can_read(chr) > 0) {
        qemu_chr_read(chr, term_fifo, 1);
        term_fifo_size = 0;
    }
    /* see if we can absorb more chars */
    if (term_fifo_size == 0)
        return 1;
    else
        return 0;
}

static void stdio_read(void *opaque)
{
    int size;
    uint8_t buf[1];
    CharDriverState *chr = opaque;

    size = read(0, buf, 1);
    if (size == 0) {
        /* stdin has been closed. Remove it from the active list.  */
        qemu_set_fd_handler2(0, NULL, NULL, NULL, NULL);
        return;
    }
    if (size > 0) {
        if (qemu_chr_can_read(chr) > 0) {
            qemu_chr_read(chr, buf, 1);
        } else if (term_fifo_size == 0) {
            term_fifo[term_fifo_size++] = buf[0];
        }
    }
}

/* init terminal so that we can grab keys */
static struct termios oldtty;
static int old_fd0_flags;
static int term_atexit_done;

static void term_exit(void)
{
    tcsetattr (0, TCSANOW, &oldtty);
    fcntl(0, F_SETFL, old_fd0_flags);
}

static void term_init(void)
{
    struct termios tty;

    tcgetattr (0, &tty);
    oldtty = tty;
    old_fd0_flags = fcntl(0, F_GETFL);

    tty.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP
                          |INLCR|IGNCR|ICRNL|IXON);
    tty.c_oflag |= OPOST;
    tty.c_lflag &= ~(ECHO|ECHONL|ICANON|IEXTEN);
    /* if graphical mode, we allow Ctrl-C handling */
    if (nographic)
        tty.c_lflag &= ~ISIG;
    tty.c_cflag &= ~(CSIZE|PARENB);
    tty.c_cflag |= CS8;
    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 0;

    tcsetattr (0, TCSANOW, &tty);

    if (!term_atexit_done++)
        atexit(term_exit);

    fcntl(0, F_SETFL, O_NONBLOCK);
}

static void qemu_chr_close_stdio(struct CharDriverState *chr)
{
    term_exit();
    stdio_nb_clients--;
    qemu_set_fd_handler2(0, NULL, NULL, NULL, NULL);
    fd_chr_close(chr);
}

static CharDriverState *qemu_chr_open_stdio(void)
{
    CharDriverState *chr;

    if (stdio_nb_clients >= STDIO_MAX_CLIENTS)
        return NULL;
    chr = qemu_chr_open_fd(0, 1);
    chr->chr_close = qemu_chr_close_stdio;
    qemu_set_fd_handler2(0, stdio_read_poll, stdio_read, NULL, chr);
    stdio_nb_clients++;
    term_init();

    return chr;
}

#ifdef __sun__
/* Once Solaris has openpty(), this is going to be removed. */
int openpty(int *amaster, int *aslave, char *name,
            struct termios *termp, struct winsize *winp)
{
        const char *slave;
        int mfd = -1, sfd = -1;

        *amaster = *aslave = -1;

        mfd = open("/dev/ptmx", O_RDWR | O_NOCTTY);
        if (mfd < 0)
                goto err;

        if (grantpt(mfd) == -1 || unlockpt(mfd) == -1)
                goto err;

        if ((slave = ptsname(mfd)) == NULL)
                goto err;

        if ((sfd = open(slave, O_RDONLY | O_NOCTTY)) == -1)
                goto err;

        if (ioctl(sfd, I_PUSH, "ptem") == -1 ||
            (termp != NULL && tcgetattr(sfd, termp) < 0))
                goto err;

        if (amaster)
                *amaster = mfd;
        if (aslave)
                *aslave = sfd;
        if (winp)
                ioctl(sfd, TIOCSWINSZ, winp);

        return 0;

err:
        if (sfd != -1)
                close(sfd);
        close(mfd);
        return -1;
}

void cfmakeraw (struct termios *termios_p)
{
        termios_p->c_iflag &=
                ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL|IXON);
        termios_p->c_oflag &= ~OPOST;
        termios_p->c_lflag &= ~(ECHO|ECHONL|ICANON|ISIG|IEXTEN);
        termios_p->c_cflag &= ~(CSIZE|PARENB);
        termios_p->c_cflag |= CS8;

        termios_p->c_cc[VMIN] = 0;
        termios_p->c_cc[VTIME] = 0;
}
#endif

#if defined(__linux__) || defined(__sun__) || defined(__FreeBSD__) \
    || defined(__NetBSD__) || defined(__OpenBSD__)

typedef struct {
    int fd;
    int connected;
    int polling;
    int read_bytes;
    QEMUTimer *timer;
} PtyCharDriver;

static void pty_chr_update_read_handler(CharDriverState *chr);
static void pty_chr_state(CharDriverState *chr, int connected);

static int pty_chr_write(CharDriverState *chr, const uint8_t *buf, int len)
{
    PtyCharDriver *s = chr->opaque;

    if (!s->connected) {
        /* guest sends data, check for (re-)connect */
        pty_chr_update_read_handler(chr);
        return 0;
    }
    return unix_write(s->fd, buf, len);
}

static int pty_chr_read_poll(void *opaque)
{
    CharDriverState *chr = opaque;
    PtyCharDriver *s = chr->opaque;

    s->read_bytes = qemu_chr_can_read(chr);
    return s->read_bytes;
}

static void pty_chr_read(void *opaque)
{
    CharDriverState *chr = opaque;
    PtyCharDriver *s = chr->opaque;
    int size, len;
    uint8_t buf[1024];

    len = sizeof(buf);
    if (len > s->read_bytes)
        len = s->read_bytes;
    if (len == 0)
        return;
    size = read(s->fd, buf, len);
    if ((size == -1 && errno == EIO) ||
        (size == 0)) {
        pty_chr_state(chr, 0);
        return;
    }
    if (size > 0) {
        pty_chr_state(chr, 1);
        qemu_chr_read(chr, buf, size);
    }
}

static void pty_chr_update_read_handler(CharDriverState *chr)
{
    PtyCharDriver *s = chr->opaque;

    qemu_set_fd_handler2(s->fd, pty_chr_read_poll,
                         pty_chr_read, NULL, chr);
    s->polling = 1;
    /*
     * Short timeout here: just need wait long enougth that qemu makes
     * it through the poll loop once.  When reconnected we want a
     * short timeout so we notice it almost instantly.  Otherwise
     * read() gives us -EIO instantly, making pty_chr_state() reset the
     * timeout to the normal (much longer) poll interval before the
     * timer triggers.
     */
    qemu_mod_timer(s->timer, qemu_get_clock(rt_clock) + 10);
}

static void pty_chr_state(CharDriverState *chr, int connected)
{
    PtyCharDriver *s = chr->opaque;

    if (!connected) {
        qemu_set_fd_handler2(s->fd, NULL, NULL, NULL, NULL);
        s->connected = 0;
        s->polling = 0;
        /* (re-)connect poll interval for idle guests: once per second.
         * We check more frequently in case the guests sends data to
         * the virtual device linked to our pty. */
        qemu_mod_timer(s->timer, qemu_get_clock(rt_clock) + 1000);
    } else {
        if (!s->connected)
            qemu_chr_reset(chr);
        s->connected = 1;
    }
}

static void pty_chr_timer(void *opaque)
{
    struct CharDriverState *chr = opaque;
    PtyCharDriver *s = chr->opaque;

    if (s->connected)
        return;
    if (s->polling) {
        /* If we arrive here without polling being cleared due
         * read returning -EIO, then we are (re-)connected */
        pty_chr_state(chr, 1);
        return;
    }

    /* Next poll ... */
    pty_chr_update_read_handler(chr);
}

static void pty_chr_close(struct CharDriverState *chr)
{
    PtyCharDriver *s = chr->opaque;

    qemu_set_fd_handler2(s->fd, NULL, NULL, NULL, NULL);
    close(s->fd);
    qemu_free(s);
}

static CharDriverState *qemu_chr_open_pty(void)
{
    CharDriverState *chr;
    PtyCharDriver *s;
    struct termios tty;
    int slave_fd;
#if defined(__OpenBSD__)
    char pty_name[PATH_MAX];
#define q_ptsname(x) pty_name
#else
    char *pty_name = NULL;
#define q_ptsname(x) ptsname(x)
#endif

    chr = qemu_mallocz(sizeof(CharDriverState));
    if (!chr)
        return NULL;
    s = qemu_mallocz(sizeof(PtyCharDriver));
    if (!s) {
        qemu_free(chr);
        return NULL;
    }

    if (openpty(&s->fd, &slave_fd, pty_name, NULL, NULL) < 0) {
        return NULL;
    }

    /* Set raw attributes on the pty. */
    cfmakeraw(&tty);
    tcsetattr(slave_fd, TCSAFLUSH, &tty);
    close(slave_fd);

    fprintf(stderr, "char device redirected to %s\n", q_ptsname(s->fd));

    chr->opaque = s;
    chr->chr_write = pty_chr_write;
    chr->chr_update_read_handler = pty_chr_update_read_handler;
    chr->chr_close = pty_chr_close;

    s->timer = qemu_new_timer(rt_clock, pty_chr_timer, chr);

    return chr;
}

static void tty_serial_init(int fd, int speed,
                            int parity, int data_bits, int stop_bits)
{
    struct termios tty;
    speed_t spd;

#if 0
    printf("tty_serial_init: speed=%d parity=%c data=%d stop=%d\n",
           speed, parity, data_bits, stop_bits);
#endif
    tcgetattr (fd, &tty);

#define MARGIN 1.1
    if (speed <= 50 * MARGIN)
        spd = B50;
    else if (speed <= 75 * MARGIN)
        spd = B75;
    else if (speed <= 300 * MARGIN)
        spd = B300;
    else if (speed <= 600 * MARGIN)
        spd = B600;
    else if (speed <= 1200 * MARGIN)
        spd = B1200;
    else if (speed <= 2400 * MARGIN)
        spd = B2400;
    else if (speed <= 4800 * MARGIN)
        spd = B4800;
    else if (speed <= 9600 * MARGIN)
        spd = B9600;
    else if (speed <= 19200 * MARGIN)
        spd = B19200;
    else if (speed <= 38400 * MARGIN)
        spd = B38400;
    else if (speed <= 57600 * MARGIN)
        spd = B57600;
    else if (speed <= 115200 * MARGIN)
        spd = B115200;
    else
        spd = B115200;

    cfsetispeed(&tty, spd);
    cfsetospeed(&tty, spd);

    tty.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP
                          |INLCR|IGNCR|ICRNL|IXON);
    tty.c_oflag |= OPOST;
    tty.c_lflag &= ~(ECHO|ECHONL|ICANON|IEXTEN|ISIG);
    tty.c_cflag &= ~(CSIZE|PARENB|PARODD|CRTSCTS|CSTOPB);
    switch(data_bits) {
    default:
    case 8:
        tty.c_cflag |= CS8;
        break;
    case 7:
        tty.c_cflag |= CS7;
        break;
    case 6:
        tty.c_cflag |= CS6;
        break;
    case 5:
        tty.c_cflag |= CS5;
        break;
    }
    switch(parity) {
    default:
    case 'N':
        break;
    case 'E':
        tty.c_cflag |= PARENB;
        break;
    case 'O':
        tty.c_cflag |= PARENB | PARODD;
        break;
    }
    if (stop_bits == 2)
        tty.c_cflag |= CSTOPB;

    tcsetattr (fd, TCSANOW, &tty);
}

static int tty_serial_ioctl(CharDriverState *chr, int cmd, void *arg)
{
    FDCharDriver *s = chr->opaque;

    switch(cmd) {
    case CHR_IOCTL_SERIAL_SET_PARAMS:
        {
            QEMUSerialSetParams *ssp = arg;
            tty_serial_init(s->fd_in, ssp->speed, ssp->parity,
                            ssp->data_bits, ssp->stop_bits);
        }
        break;
    case CHR_IOCTL_SERIAL_SET_BREAK:
        {
            int enable = *(int *)arg;
            if (enable)
                tcsendbreak(s->fd_in, 1);
        }
        break;
    case CHR_IOCTL_SERIAL_GET_TIOCM:
        {
            int sarg = 0;
            int *targ = (int *)arg;
            ioctl(s->fd_in, TIOCMGET, &sarg);
            *targ = 0;
            if (sarg | TIOCM_CTS)
                *targ |= CHR_TIOCM_CTS;
            if (sarg | TIOCM_CAR)
                *targ |= CHR_TIOCM_CAR;
            if (sarg | TIOCM_DSR)
                *targ |= CHR_TIOCM_DSR;
            if (sarg | TIOCM_RI)
                *targ |= CHR_TIOCM_RI;
            if (sarg | TIOCM_DTR)
                *targ |= CHR_TIOCM_DTR;
            if (sarg | TIOCM_RTS)
                *targ |= CHR_TIOCM_RTS;
        }
        break;
    case CHR_IOCTL_SERIAL_SET_TIOCM:
        {
            int sarg = *(int *)arg;
            int targ = 0;
            if (sarg | CHR_TIOCM_DTR)
                targ |= TIOCM_DTR;
            if (sarg | CHR_TIOCM_RTS)
                targ |= TIOCM_RTS;
            ioctl(s->fd_in, TIOCMSET, &targ);
        }
        break;
    default:
        return -ENOTSUP;
    }
    return 0;
}

static CharDriverState *qemu_chr_open_tty(const char *filename)
{
    CharDriverState *chr;
    int fd;

    TFR(fd = open(filename, O_RDWR | O_NONBLOCK));
    tty_serial_init(fd, 115200, 'N', 8, 1);
    chr = qemu_chr_open_fd(fd, fd);
    if (!chr) {
        close(fd);
        return NULL;
    }
    chr->chr_ioctl = tty_serial_ioctl;
    qemu_chr_reset(chr);
    return chr;
}
#else  /* ! __linux__ && ! __sun__ */
static CharDriverState *qemu_chr_open_pty(void)
{
    return NULL;
}
#endif /* __linux__ || __sun__ */

#if defined(__linux__)
typedef struct {
    int fd;
    int mode;
} ParallelCharDriver;

static int pp_hw_mode(ParallelCharDriver *s, uint16_t mode)
{
    if (s->mode != mode) {
	int m = mode;
        if (ioctl(s->fd, PPSETMODE, &m) < 0)
            return 0;
	s->mode = mode;
    }
    return 1;
}

static int pp_ioctl(CharDriverState *chr, int cmd, void *arg)
{
    ParallelCharDriver *drv = chr->opaque;
    int fd = drv->fd;
    uint8_t b;

    switch(cmd) {
    case CHR_IOCTL_PP_READ_DATA:
        if (ioctl(fd, PPRDATA, &b) < 0)
            return -ENOTSUP;
        *(uint8_t *)arg = b;
        break;
    case CHR_IOCTL_PP_WRITE_DATA:
        b = *(uint8_t *)arg;
        if (ioctl(fd, PPWDATA, &b) < 0)
            return -ENOTSUP;
        break;
    case CHR_IOCTL_PP_READ_CONTROL:
        if (ioctl(fd, PPRCONTROL, &b) < 0)
            return -ENOTSUP;
	/* Linux gives only the lowest bits, and no way to know data
	   direction! For better compatibility set the fixed upper
	   bits. */
        *(uint8_t *)arg = b | 0xc0;
        break;
    case CHR_IOCTL_PP_WRITE_CONTROL:
        b = *(uint8_t *)arg;
        if (ioctl(fd, PPWCONTROL, &b) < 0)
            return -ENOTSUP;
        break;
    case CHR_IOCTL_PP_READ_STATUS:
        if (ioctl(fd, PPRSTATUS, &b) < 0)
            return -ENOTSUP;
        *(uint8_t *)arg = b;
        break;
    case CHR_IOCTL_PP_DATA_DIR:
        if (ioctl(fd, PPDATADIR, (int *)arg) < 0)
            return -ENOTSUP;
        break;
    case CHR_IOCTL_PP_EPP_READ_ADDR:
	if (pp_hw_mode(drv, IEEE1284_MODE_EPP|IEEE1284_ADDR)) {
	    struct ParallelIOArg *parg = arg;
	    int n = read(fd, parg->buffer, parg->count);
	    if (n != parg->count) {
		return -EIO;
	    }
	}
        break;
    case CHR_IOCTL_PP_EPP_READ:
	if (pp_hw_mode(drv, IEEE1284_MODE_EPP)) {
	    struct ParallelIOArg *parg = arg;
	    int n = read(fd, parg->buffer, parg->count);
	    if (n != parg->count) {
		return -EIO;
	    }
	}
        break;
    case CHR_IOCTL_PP_EPP_WRITE_ADDR:
	if (pp_hw_mode(drv, IEEE1284_MODE_EPP|IEEE1284_ADDR)) {
	    struct ParallelIOArg *parg = arg;
	    int n = write(fd, parg->buffer, parg->count);
	    if (n != parg->count) {
		return -EIO;
	    }
	}
        break;
    case CHR_IOCTL_PP_EPP_WRITE:
	if (pp_hw_mode(drv, IEEE1284_MODE_EPP)) {
	    struct ParallelIOArg *parg = arg;
	    int n = write(fd, parg->buffer, parg->count);
	    if (n != parg->count) {
		return -EIO;
	    }
	}
        break;
    default:
        return -ENOTSUP;
    }
    return 0;
}

static void pp_close(CharDriverState *chr)
{
    ParallelCharDriver *drv = chr->opaque;
    int fd = drv->fd;

    pp_hw_mode(drv, IEEE1284_MODE_COMPAT);
    ioctl(fd, PPRELEASE);
    close(fd);
    qemu_free(drv);
}

static CharDriverState *qemu_chr_open_pp(const char *filename)
{
    CharDriverState *chr;
    ParallelCharDriver *drv;
    int fd;

    TFR(fd = open(filename, O_RDWR));
    if (fd < 0)
        return NULL;

    if (ioctl(fd, PPCLAIM) < 0) {
        close(fd);
        return NULL;
    }

    drv = qemu_mallocz(sizeof(ParallelCharDriver));
    if (!drv) {
        close(fd);
        return NULL;
    }
    drv->fd = fd;
    drv->mode = IEEE1284_MODE_COMPAT;

    chr = qemu_mallocz(sizeof(CharDriverState));
    if (!chr) {
	qemu_free(drv);
        close(fd);
        return NULL;
    }
    chr->chr_write = null_chr_write;
    chr->chr_ioctl = pp_ioctl;
    chr->chr_close = pp_close;
    chr->opaque = drv;

    qemu_chr_reset(chr);

    return chr;
}
#endif /* __linux__ */

#else /* _WIN32 */

typedef struct {
    int max_size;
    HANDLE hcom, hrecv, hsend;
    OVERLAPPED orecv, osend;
    BOOL fpipe;
    DWORD len;
} WinCharState;

#define NSENDBUF 2048
#define NRECVBUF 2048
#define MAXCONNECT 1
#define NTIMEOUT 5000

static int win_chr_poll(void *opaque);
static int win_chr_pipe_poll(void *opaque);

static void win_chr_close(CharDriverState *chr)
{
    WinCharState *s = chr->opaque;

    if (s->hsend) {
        CloseHandle(s->hsend);
        s->hsend = NULL;
    }
    if (s->hrecv) {
        CloseHandle(s->hrecv);
        s->hrecv = NULL;
    }
    if (s->hcom) {
        CloseHandle(s->hcom);
        s->hcom = NULL;
    }
    if (s->fpipe)
        qemu_del_polling_cb(win_chr_pipe_poll, chr);
    else
        qemu_del_polling_cb(win_chr_poll, chr);
}

static int win_chr_init(CharDriverState *chr, const char *filename)
{
    WinCharState *s = chr->opaque;
    COMMCONFIG comcfg;
    COMMTIMEOUTS cto = { 0, 0, 0, 0, 0};
    COMSTAT comstat;
    DWORD size;
    DWORD err;

    s->hsend = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!s->hsend) {
        fprintf(stderr, "Failed CreateEvent\n");
        goto fail;
    }
    s->hrecv = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!s->hrecv) {
        fprintf(stderr, "Failed CreateEvent\n");
        goto fail;
    }

    s->hcom = CreateFile(filename, GENERIC_READ|GENERIC_WRITE, 0, NULL,
                      OPEN_EXISTING, FILE_FLAG_OVERLAPPED, 0);
    if (s->hcom == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Failed CreateFile (%lu)\n", GetLastError());
        s->hcom = NULL;
        goto fail;
    }

    if (!SetupComm(s->hcom, NRECVBUF, NSENDBUF)) {
        fprintf(stderr, "Failed SetupComm\n");
        goto fail;
    }

    ZeroMemory(&comcfg, sizeof(COMMCONFIG));
    size = sizeof(COMMCONFIG);
    GetDefaultCommConfig(filename, &comcfg, &size);
    comcfg.dcb.DCBlength = sizeof(DCB);
    CommConfigDialog(filename, NULL, &comcfg);

    if (!SetCommState(s->hcom, &comcfg.dcb)) {
        fprintf(stderr, "Failed SetCommState\n");
        goto fail;
    }

    if (!SetCommMask(s->hcom, EV_ERR)) {
        fprintf(stderr, "Failed SetCommMask\n");
        goto fail;
    }

    cto.ReadIntervalTimeout = MAXDWORD;
    if (!SetCommTimeouts(s->hcom, &cto)) {
        fprintf(stderr, "Failed SetCommTimeouts\n");
        goto fail;
    }

    if (!ClearCommError(s->hcom, &err, &comstat)) {
        fprintf(stderr, "Failed ClearCommError\n");
        goto fail;
    }
    qemu_add_polling_cb(win_chr_poll, chr);
    return 0;

 fail:
    win_chr_close(chr);
    return -1;
}

static int win_chr_write(CharDriverState *chr, const uint8_t *buf, int len1)
{
    WinCharState *s = chr->opaque;
    DWORD len, ret, size, err;

    len = len1;
    ZeroMemory(&s->osend, sizeof(s->osend));
    s->osend.hEvent = s->hsend;
    while (len > 0) {
        if (s->hsend)
            ret = WriteFile(s->hcom, buf, len, &size, &s->osend);
        else
            ret = WriteFile(s->hcom, buf, len, &size, NULL);
        if (!ret) {
            err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                ret = GetOverlappedResult(s->hcom, &s->osend, &size, TRUE);
                if (ret) {
                    buf += size;
                    len -= size;
                } else {
                    break;
                }
            } else {
                break;
            }
        } else {
            buf += size;
            len -= size;
        }
    }
    return len1 - len;
}

static int win_chr_read_poll(CharDriverState *chr)
{
    WinCharState *s = chr->opaque;

    s->max_size = qemu_chr_can_read(chr);
    return s->max_size;
}

static void win_chr_readfile(CharDriverState *chr)
{
    WinCharState *s = chr->opaque;
    int ret, err;
    uint8_t buf[1024];
    DWORD size;

    ZeroMemory(&s->orecv, sizeof(s->orecv));
    s->orecv.hEvent = s->hrecv;
    ret = ReadFile(s->hcom, buf, s->len, &size, &s->orecv);
    if (!ret) {
        err = GetLastError();
        if (err == ERROR_IO_PENDING) {
            ret = GetOverlappedResult(s->hcom, &s->orecv, &size, TRUE);
        }
    }

    if (size > 0) {
        qemu_chr_read(chr, buf, size);
    }
}

static void win_chr_read(CharDriverState *chr)
{
    WinCharState *s = chr->opaque;

    if (s->len > s->max_size)
        s->len = s->max_size;
    if (s->len == 0)
        return;

    win_chr_readfile(chr);
}

static int win_chr_poll(void *opaque)
{
    CharDriverState *chr = opaque;
    WinCharState *s = chr->opaque;
    COMSTAT status;
    DWORD comerr;

    ClearCommError(s->hcom, &comerr, &status);
    if (status.cbInQue > 0) {
        s->len = status.cbInQue;
        win_chr_read_poll(chr);
        win_chr_read(chr);
        return 1;
    }
    return 0;
}

static CharDriverState *qemu_chr_open_win(const char *filename)
{
    CharDriverState *chr;
    WinCharState *s;

    chr = qemu_mallocz(sizeof(CharDriverState));
    if (!chr)
        return NULL;
    s = qemu_mallocz(sizeof(WinCharState));
    if (!s) {
        free(chr);
        return NULL;
    }
    chr->opaque = s;
    chr->chr_write = win_chr_write;
    chr->chr_close = win_chr_close;

    if (win_chr_init(chr, filename) < 0) {
        free(s);
        free(chr);
        return NULL;
    }
    qemu_chr_reset(chr);
    return chr;
}

static int win_chr_pipe_poll(void *opaque)
{
    CharDriverState *chr = opaque;
    WinCharState *s = chr->opaque;
    DWORD size;

    PeekNamedPipe(s->hcom, NULL, 0, NULL, &size, NULL);
    if (size > 0) {
        s->len = size;
        win_chr_read_poll(chr);
        win_chr_read(chr);
        return 1;
    }
    return 0;
}

static int win_chr_pipe_init(CharDriverState *chr, const char *filename)
{
    WinCharState *s = chr->opaque;
    OVERLAPPED ov;
    int ret;
    DWORD size;
    char openname[256];

    s->fpipe = TRUE;

    s->hsend = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!s->hsend) {
        fprintf(stderr, "Failed CreateEvent\n");
        goto fail;
    }
    s->hrecv = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!s->hrecv) {
        fprintf(stderr, "Failed CreateEvent\n");
        goto fail;
    }

    snprintf(openname, sizeof(openname), "\\\\.\\pipe\\%s", filename);
    s->hcom = CreateNamedPipe(openname, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                              PIPE_TYPE_BYTE | PIPE_READMODE_BYTE |
                              PIPE_WAIT,
                              MAXCONNECT, NSENDBUF, NRECVBUF, NTIMEOUT, NULL);
    if (s->hcom == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Failed CreateNamedPipe (%lu)\n", GetLastError());
        s->hcom = NULL;
        goto fail;
    }

    ZeroMemory(&ov, sizeof(ov));
    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    ret = ConnectNamedPipe(s->hcom, &ov);
    if (ret) {
        fprintf(stderr, "Failed ConnectNamedPipe\n");
        goto fail;
    }

    ret = GetOverlappedResult(s->hcom, &ov, &size, TRUE);
    if (!ret) {
        fprintf(stderr, "Failed GetOverlappedResult\n");
        if (ov.hEvent) {
            CloseHandle(ov.hEvent);
            ov.hEvent = NULL;
        }
        goto fail;
    }

    if (ov.hEvent) {
        CloseHandle(ov.hEvent);
        ov.hEvent = NULL;
    }
    qemu_add_polling_cb(win_chr_pipe_poll, chr);
    return 0;

 fail:
    win_chr_close(chr);
    return -1;
}


static CharDriverState *qemu_chr_open_win_pipe(const char *filename)
{
    CharDriverState *chr;
    WinCharState *s;

    chr = qemu_mallocz(sizeof(CharDriverState));
    if (!chr)
        return NULL;
    s = qemu_mallocz(sizeof(WinCharState));
    if (!s) {
        free(chr);
        return NULL;
    }
    chr->opaque = s;
    chr->chr_write = win_chr_write;
    chr->chr_close = win_chr_close;

    if (win_chr_pipe_init(chr, filename) < 0) {
        free(s);
        free(chr);
        return NULL;
    }
    qemu_chr_reset(chr);
    return chr;
}

static CharDriverState *qemu_chr_open_win_file(HANDLE fd_out)
{
    CharDriverState *chr;
    WinCharState *s;

    chr = qemu_mallocz(sizeof(CharDriverState));
    if (!chr)
        return NULL;
    s = qemu_mallocz(sizeof(WinCharState));
    if (!s) {
        free(chr);
        return NULL;
    }
    s->hcom = fd_out;
    chr->opaque = s;
    chr->chr_write = win_chr_write;
    qemu_chr_reset(chr);
    return chr;
}

static CharDriverState *qemu_chr_open_win_con(const char *filename)
{
    return qemu_chr_open_win_file(GetStdHandle(STD_OUTPUT_HANDLE));
}

static CharDriverState *qemu_chr_open_win_file_out(const char *file_out)
{
    HANDLE fd_out;

    fd_out = CreateFile(file_out, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (fd_out == INVALID_HANDLE_VALUE)
        return NULL;

    return qemu_chr_open_win_file(fd_out);
}
#endif /* !_WIN32 */

/***********************************************************/
/* UDP Net console */

typedef struct {
    int fd;
    struct sockaddr_in daddr;
    uint8_t buf[1024];
    int bufcnt;
    int bufptr;
    int max_size;
} NetCharDriver;

static int udp_chr_write(CharDriverState *chr, const uint8_t *buf, int len)
{
    NetCharDriver *s = chr->opaque;

    return sendto(s->fd, buf, len, 0,
                  (struct sockaddr *)&s->daddr, sizeof(struct sockaddr_in));
}

static int udp_chr_read_poll(void *opaque)
{
    CharDriverState *chr = opaque;
    NetCharDriver *s = chr->opaque;

    s->max_size = qemu_chr_can_read(chr);

    /* If there were any stray characters in the queue process them
     * first
     */
    while (s->max_size > 0 && s->bufptr < s->bufcnt) {
        qemu_chr_read(chr, &s->buf[s->bufptr], 1);
        s->bufptr++;
        s->max_size = qemu_chr_can_read(chr);
    }
    return s->max_size;
}

static void udp_chr_read(void *opaque)
{
    CharDriverState *chr = opaque;
    NetCharDriver *s = chr->opaque;

    if (s->max_size == 0)
        return;
    s->bufcnt = recv(s->fd, s->buf, sizeof(s->buf), 0);
    s->bufptr = s->bufcnt;
    if (s->bufcnt <= 0)
        return;

    s->bufptr = 0;
    while (s->max_size > 0 && s->bufptr < s->bufcnt) {
        qemu_chr_read(chr, &s->buf[s->bufptr], 1);
        s->bufptr++;
        s->max_size = qemu_chr_can_read(chr);
    }
}

static void udp_chr_update_read_handler(CharDriverState *chr)
{
    NetCharDriver *s = chr->opaque;

    if (s->fd >= 0) {
        qemu_set_fd_handler2(s->fd, udp_chr_read_poll,
                             udp_chr_read, NULL, chr);
    }
}

int parse_host_port(struct sockaddr_in *saddr, const char *str);
#ifndef _WIN32
static int parse_unix_path(struct sockaddr_un *uaddr, const char *str);
#endif
int parse_host_src_port(struct sockaddr_in *haddr,
                        struct sockaddr_in *saddr,
                        const char *str);

static CharDriverState *qemu_chr_open_udp(const char *def)
{
    CharDriverState *chr = NULL;
    NetCharDriver *s = NULL;
    int fd = -1;
    struct sockaddr_in saddr;

    chr = qemu_mallocz(sizeof(CharDriverState));
    if (!chr)
        goto return_err;
    s = qemu_mallocz(sizeof(NetCharDriver));
    if (!s)
        goto return_err;

    fd = socket(PF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket(PF_INET, SOCK_DGRAM)");
        goto return_err;
    }

    if (parse_host_src_port(&s->daddr, &saddr, def) < 0) {
        printf("Could not parse: %s\n", def);
        goto return_err;
    }

    if (bind(fd, (struct sockaddr *)&saddr, sizeof(saddr)) < 0)
    {
        perror("bind");
        goto return_err;
    }

    s->fd = fd;
    s->bufcnt = 0;
    s->bufptr = 0;
    chr->opaque = s;
    chr->chr_write = udp_chr_write;
    chr->chr_update_read_handler = udp_chr_update_read_handler;
    return chr;

return_err:
    if (chr)
        free(chr);
    if (s)
        free(s);
    if (fd >= 0)
        closesocket(fd);
    return NULL;
}

/***********************************************************/
/* TCP Net console */

typedef struct {
    int fd, listen_fd;
    int connected;
    int max_size;
    int do_telnetopt;
    int do_nodelay;
    int is_unix;
} TCPCharDriver;

static void tcp_chr_accept(void *opaque);

static int tcp_chr_write(CharDriverState *chr, const uint8_t *buf, int len)
{
    TCPCharDriver *s = chr->opaque;
    if (s->connected) {
        return send_all(s->fd, buf, len);
    } else {
        /* XXX: indicate an error ? */
        return len;
    }
}

static int tcp_chr_read_poll(void *opaque)
{
    CharDriverState *chr = opaque;
    TCPCharDriver *s = chr->opaque;
    if (!s->connected)
        return 0;
    s->max_size = qemu_chr_can_read(chr);
    return s->max_size;
}

#define IAC 255
#define IAC_BREAK 243
static void tcp_chr_process_IAC_bytes(CharDriverState *chr,
                                      TCPCharDriver *s,
                                      uint8_t *buf, int *size)
{
    /* Handle any telnet client's basic IAC options to satisfy char by
     * char mode with no echo.  All IAC options will be removed from
     * the buf and the do_telnetopt variable will be used to track the
     * state of the width of the IAC information.
     *
     * IAC commands come in sets of 3 bytes with the exception of the
     * "IAC BREAK" command and the double IAC.
     */

    int i;
    int j = 0;

    for (i = 0; i < *size; i++) {
        if (s->do_telnetopt > 1) {
            if ((unsigned char)buf[i] == IAC && s->do_telnetopt == 2) {
                /* Double IAC means send an IAC */
                if (j != i)
                    buf[j] = buf[i];
                j++;
                s->do_telnetopt = 1;
            } else {
                if ((unsigned char)buf[i] == IAC_BREAK && s->do_telnetopt == 2) {
                    /* Handle IAC break commands by sending a serial break */
                    qemu_chr_event(chr, CHR_EVENT_BREAK);
                    s->do_telnetopt++;
                }
                s->do_telnetopt++;
            }
            if (s->do_telnetopt >= 4) {
                s->do_telnetopt = 1;
            }
        } else {
            if ((unsigned char)buf[i] == IAC) {
                s->do_telnetopt = 2;
            } else {
                if (j != i)
                    buf[j] = buf[i];
                j++;
            }
        }
    }
    *size = j;
}

static void tcp_chr_read(void *opaque)
{
    CharDriverState *chr = opaque;
    TCPCharDriver *s = chr->opaque;
    uint8_t buf[1024];
    int len, size;

    if (!s->connected || s->max_size <= 0)
        return;
    len = sizeof(buf);
    if (len > s->max_size)
        len = s->max_size;
    size = recv(s->fd, buf, len, 0);
    if (size == 0) {
        /* connection closed */
        s->connected = 0;
        if (s->listen_fd >= 0) {
            qemu_set_fd_handler(s->listen_fd, tcp_chr_accept, NULL, chr);
        }
        qemu_set_fd_handler(s->fd, NULL, NULL, NULL);
        closesocket(s->fd);
        s->fd = -1;
    } else if (size > 0) {
        if (s->do_telnetopt)
            tcp_chr_process_IAC_bytes(chr, s, buf, &size);
        if (size > 0)
            qemu_chr_read(chr, buf, size);
    }
}

static void tcp_chr_connect(void *opaque)
{
    CharDriverState *chr = opaque;
    TCPCharDriver *s = chr->opaque;

    s->connected = 1;
    qemu_set_fd_handler2(s->fd, tcp_chr_read_poll,
                         tcp_chr_read, NULL, chr);
    qemu_chr_reset(chr);
}

#define IACSET(x,a,b,c) x[0] = a; x[1] = b; x[2] = c;
static void tcp_chr_telnet_init(int fd)
{
    char buf[3];
    /* Send the telnet negotion to put telnet in binary, no echo, single char mode */
    IACSET(buf, 0xff, 0xfb, 0x01);  /* IAC WILL ECHO */
    send(fd, (char *)buf, 3, 0);
    IACSET(buf, 0xff, 0xfb, 0x03);  /* IAC WILL Suppress go ahead */
    send(fd, (char *)buf, 3, 0);
    IACSET(buf, 0xff, 0xfb, 0x00);  /* IAC WILL Binary */
    send(fd, (char *)buf, 3, 0);
    IACSET(buf, 0xff, 0xfd, 0x00);  /* IAC DO Binary */
    send(fd, (char *)buf, 3, 0);
}

static void socket_set_nodelay(int fd)
{
    int val = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char *)&val, sizeof(val));
}

static void tcp_chr_accept(void *opaque)
{
    CharDriverState *chr = opaque;
    TCPCharDriver *s = chr->opaque;
    struct sockaddr_in saddr;
#ifndef _WIN32
    struct sockaddr_un uaddr;
#endif
    struct sockaddr *addr;
    socklen_t len;
    int fd;

    for(;;) {
#ifndef _WIN32
	if (s->is_unix) {
	    len = sizeof(uaddr);
	    addr = (struct sockaddr *)&uaddr;
	} else
#endif
	{
	    len = sizeof(saddr);
	    addr = (struct sockaddr *)&saddr;
	}
        fd = accept(s->listen_fd, addr, &len);
        if (fd < 0 && errno != EINTR) {
            return;
        } else if (fd >= 0) {
            if (s->do_telnetopt)
                tcp_chr_telnet_init(fd);
            break;
        }
    }
    socket_set_nonblock(fd);
    if (s->do_nodelay)
        socket_set_nodelay(fd);
    s->fd = fd;
    qemu_set_fd_handler(s->listen_fd, NULL, NULL, NULL);
    tcp_chr_connect(chr);
}

static void tcp_chr_close(CharDriverState *chr)
{
    TCPCharDriver *s = chr->opaque;
    if (s->fd >= 0)
        closesocket(s->fd);
    if (s->listen_fd >= 0)
        closesocket(s->listen_fd);
    qemu_free(s);
}

static CharDriverState *qemu_chr_open_tcp(const char *host_str,
                                          int is_telnet,
					  int is_unix)
{
    CharDriverState *chr = NULL;
    TCPCharDriver *s = NULL;
    int fd = -1, ret, err, val;
    int is_listen = 0;
    int is_waitconnect = 1;
    int do_nodelay = 0;
    const char *ptr;
    struct sockaddr_in saddr;
#ifndef _WIN32
    struct sockaddr_un uaddr;
#endif
    struct sockaddr *addr;
    socklen_t addrlen;

#ifndef _WIN32
    if (is_unix) {
	addr = (struct sockaddr *)&uaddr;
	addrlen = sizeof(uaddr);
	if (parse_unix_path(&uaddr, host_str) < 0)
	    goto fail;
    } else
#endif
    {
	addr = (struct sockaddr *)&saddr;
	addrlen = sizeof(saddr);
	if (parse_host_port(&saddr, host_str) < 0)
	    goto fail;
    }

    ptr = host_str;
    while((ptr = strchr(ptr,','))) {
        ptr++;
        if (!strncmp(ptr,"server",6)) {
            is_listen = 1;
        } else if (!strncmp(ptr,"nowait",6)) {
            is_waitconnect = 0;
        } else if (!strncmp(ptr,"nodelay",6)) {
            do_nodelay = 1;
        } else {
            printf("Unknown option: %s\n", ptr);
            goto fail;
        }
    }
    if (!is_listen)
        is_waitconnect = 0;

    chr = qemu_mallocz(sizeof(CharDriverState));
    if (!chr)
        goto fail;
    s = qemu_mallocz(sizeof(TCPCharDriver));
    if (!s)
        goto fail;

#ifndef _WIN32
    if (is_unix)
	fd = socket(PF_UNIX, SOCK_STREAM, 0);
    else
#endif
	fd = socket(PF_INET, SOCK_STREAM, 0);

    if (fd < 0)
        goto fail;

    if (!is_waitconnect)
        socket_set_nonblock(fd);

    s->connected = 0;
    s->fd = -1;
    s->listen_fd = -1;
    s->is_unix = is_unix;
    s->do_nodelay = do_nodelay && !is_unix;

    chr->opaque = s;
    chr->chr_write = tcp_chr_write;
    chr->chr_close = tcp_chr_close;

    if (is_listen) {
        /* allow fast reuse */
#ifndef _WIN32
	if (is_unix) {
	    char path[109];
	    pstrcpy(path, sizeof(path), uaddr.sun_path);
	    unlink(path);
	} else
#endif
	{
	    val = 1;
	    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&val, sizeof(val));
	}

        ret = bind(fd, addr, addrlen);
        if (ret < 0)
            goto fail;

        ret = listen(fd, 0);
        if (ret < 0)
            goto fail;

        s->listen_fd = fd;
        qemu_set_fd_handler(s->listen_fd, tcp_chr_accept, NULL, chr);
        if (is_telnet)
            s->do_telnetopt = 1;
    } else {
        for(;;) {
            ret = connect(fd, addr, addrlen);
            if (ret < 0) {
                err = socket_error();
                if (err == EINTR || err == EWOULDBLOCK) {
                } else if (err == EINPROGRESS) {
                    break;
#ifdef _WIN32
                } else if (err == WSAEALREADY) {
                    break;
#endif
                } else {
                    goto fail;
                }
            } else {
                s->connected = 1;
                break;
            }
        }
        s->fd = fd;
        socket_set_nodelay(fd);
        if (s->connected)
            tcp_chr_connect(chr);
        else
            qemu_set_fd_handler(s->fd, NULL, tcp_chr_connect, chr);
    }

    if (is_listen && is_waitconnect) {
        printf("QEMU waiting for connection on: %s\n", host_str);
        tcp_chr_accept(chr);
        socket_set_nonblock(s->listen_fd);
    }

    return chr;
 fail:
    if (fd >= 0)
        closesocket(fd);
    qemu_free(s);
    qemu_free(chr);
    return NULL;
}

CharDriverState *qemu_chr_open(const char *filename)
{
    const char *p;

    if (!strcmp(filename, "vc")) {
        return text_console_init(&display_state, 0);
    } else if (strstart(filename, "vc:", &p)) {
        return text_console_init(&display_state, p);
    } else if (!strcmp(filename, "null")) {
        return qemu_chr_open_null();
    } else
    if (strstart(filename, "tcp:", &p)) {
        return qemu_chr_open_tcp(p, 0, 0);
    } else
    if (strstart(filename, "telnet:", &p)) {
        return qemu_chr_open_tcp(p, 1, 0);
    } else
    if (strstart(filename, "udp:", &p)) {
        return qemu_chr_open_udp(p);
    } else
    if (strstart(filename, "mon:", &p)) {
        CharDriverState *drv = qemu_chr_open(p);
        if (drv) {
            drv = qemu_chr_open_mux(drv);
            monitor_init(drv, !nographic);
            return drv;
        }
        printf("Unable to open driver: %s\n", p);
        return 0;
    } else
#ifndef _WIN32
    if (strstart(filename, "unix:", &p)) {
	return qemu_chr_open_tcp(p, 0, 1);
    } else if (strstart(filename, "file:", &p)) {
        return qemu_chr_open_file_out(p);
    } else if (strstart(filename, "pipe:", &p)) {
        return qemu_chr_open_pipe(p);
    } else if (!strcmp(filename, "pty")) {
        return qemu_chr_open_pty();
    } else if (!strcmp(filename, "stdio")) {
        return qemu_chr_open_stdio();
    } else
#if defined(__linux__)
    if (strstart(filename, "/dev/parport", NULL)) {
        return qemu_chr_open_pp(filename);
    } else
#endif
#if defined(__linux__) || defined(__sun__) || defined(__FreeBSD__) \
    || defined(__NetBSD__) || defined(__OpenBSD__)
    if (strstart(filename, "/dev/", NULL)) {
        return qemu_chr_open_tty(filename);
    } else
#endif
#else /* !_WIN32 */
    if (strstart(filename, "COM", NULL)) {
        return qemu_chr_open_win(filename);
    } else
    if (strstart(filename, "pipe:", &p)) {
        return qemu_chr_open_win_pipe(p);
    } else
    if (strstart(filename, "con:", NULL)) {
        return qemu_chr_open_win_con(filename);
    } else
    if (strstart(filename, "file:", &p)) {
        return qemu_chr_open_win_file_out(p);
    } else
#endif
#ifdef CONFIG_BRLAPI
    if (!strcmp(filename, "braille")) {
        return chr_baum_init();
    } else
#endif
    {
        return NULL;
    }
}

void qemu_chr_close(CharDriverState *chr)
{
    if (chr->chr_close)
        chr->chr_close(chr);
    qemu_free(chr);
}

/***********************************************************/
/* network device redirectors */

#if defined(DEBUG_NET) || defined(DEBUG_SLIRP)
static void hex_dump(FILE *f, const uint8_t *buf, int size)
{
    int len, i, j, c;

    for(i=0;i<size;i+=16) {
        len = size - i;
        if (len > 16)
            len = 16;
        fprintf(f, "%08x ", i);
        for(j=0;j<16;j++) {
            if (j < len)
                fprintf(f, " %02x", buf[i+j]);
            else
                fprintf(f, "   ");
        }
        fprintf(f, " ");
        for(j=0;j<len;j++) {
            c = buf[i+j];
            if (c < ' ' || c > '~')
                c = '.';
            fprintf(f, "%c", c);
        }
        fprintf(f, "\n");
    }
}
#endif

static int parse_macaddr(uint8_t *macaddr, const char *p)
{
    int i;
    char *last_char;
    long int offset;

    errno = 0;
    offset = strtol(p, &last_char, 0);    
    if (0 == errno && '\0' == *last_char &&
            offset >= 0 && offset <= 0xFFFFFF) {
        macaddr[3] = (offset & 0xFF0000) >> 16;
        macaddr[4] = (offset & 0xFF00) >> 8;
        macaddr[5] = offset & 0xFF;
        return 0;
    } else {
        for(i = 0; i < 6; i++) {
            macaddr[i] = strtol(p, (char **)&p, 16);
            if (i == 5) {
                if (*p != '\0')
                    return -1;
            } else {
                if (*p != ':' && *p != '-')
                    return -1;
                p++;
            }
        }
        return 0;    
    }

    return -1;
}

static int get_str_sep(char *buf, int buf_size, const char **pp, int sep)
{
    const char *p, *p1;
    int len;
    p = *pp;
    p1 = strchr(p, sep);
    if (!p1)
        return -1;
    len = p1 - p;
    p1++;
    if (buf_size > 0) {
        if (len > buf_size - 1)
            len = buf_size - 1;
        memcpy(buf, p, len);
        buf[len] = '\0';
    }
    *pp = p1;
    return 0;
}

int parse_host_src_port(struct sockaddr_in *haddr,
                        struct sockaddr_in *saddr,
                        const char *input_str)
{
    char *str = strdup(input_str);
    char *host_str = str;
    char *src_str;
    const char *src_str2;
    char *ptr;

    /*
     * Chop off any extra arguments at the end of the string which
     * would start with a comma, then fill in the src port information
     * if it was provided else use the "any address" and "any port".
     */
    if ((ptr = strchr(str,',')))
        *ptr = '\0';

    if ((src_str = strchr(input_str,'@'))) {
        *src_str = '\0';
        src_str++;
    }

    if (parse_host_port(haddr, host_str) < 0)
        goto fail;

    src_str2 = src_str;
    if (!src_str || *src_str == '\0')
        src_str2 = ":0";

    if (parse_host_port(saddr, src_str2) < 0)
        goto fail;

    free(str);
    return(0);

fail:
    free(str);
    return -1;
}

int parse_host_port(struct sockaddr_in *saddr, const char *str)
{
    char buf[512];
    struct hostent *he;
    const char *p, *r;
    int port;

    p = str;
    if (get_str_sep(buf, sizeof(buf), &p, ':') < 0)
        return -1;
    saddr->sin_family = AF_INET;
    if (buf[0] == '\0') {
        saddr->sin_addr.s_addr = 0;
    } else {
        if (isdigit(buf[0])) {
            if (!inet_aton(buf, &saddr->sin_addr))
                return -1;
        } else {
            if ((he = gethostbyname(buf)) == NULL)
                return - 1;
            saddr->sin_addr = *(struct in_addr *)he->h_addr;
        }
    }
    port = strtol(p, (char **)&r, 0);
    if (r == p)
        return -1;
    saddr->sin_port = htons(port);
    return 0;
}

#ifndef _WIN32
static int parse_unix_path(struct sockaddr_un *uaddr, const char *str)
{
    const char *p;
    int len;

    len = MIN(108, strlen(str));
    p = strchr(str, ',');
    if (p)
	len = MIN(len, p - str);

    memset(uaddr, 0, sizeof(*uaddr));

    uaddr->sun_family = AF_UNIX;
    memcpy(uaddr->sun_path, str, len);

    return 0;
}
#endif

/* find or alloc a new VLAN */
VLANState *qemu_find_vlan(int id)
{
    VLANState **pvlan, *vlan;
    for(vlan = first_vlan; vlan != NULL; vlan = vlan->next) {
        if (vlan->id == id)
            return vlan;
    }
    vlan = qemu_mallocz(sizeof(VLANState));
    if (!vlan)
        return NULL;
    vlan->id = id;
    vlan->next = NULL;
    pvlan = &first_vlan;
    while (*pvlan != NULL)
        pvlan = &(*pvlan)->next;
    *pvlan = vlan;
    return vlan;
}

VLANClientState *qemu_new_vlan_client(VLANState *vlan,
                                      IOReadHandler *fd_read,
                                      IOCanRWHandler *fd_can_read,
                                      void *opaque)
{
    VLANClientState *vc, **pvc;
    vc = qemu_mallocz(sizeof(VLANClientState));
    if (!vc)
        return NULL;
    vc->fd_read = fd_read;
    vc->fd_can_read = fd_can_read;
    vc->opaque = opaque;
    vc->vlan = vlan;

    vc->next = NULL;
    pvc = &vlan->first_client;
    while (*pvc != NULL)
        pvc = &(*pvc)->next;
    *pvc = vc;
    return vc;
}

void qemu_del_vlan_client(VLANClientState *vc)
{
    VLANClientState **pvc = &vc->vlan->first_client;

    while (*pvc != NULL)
        if (*pvc == vc) {
            *pvc = vc->next;
            free(vc);
            break;
        } else
            pvc = &(*pvc)->next;
}

int qemu_can_send_packet(VLANClientState *vc1)
{
    VLANState *vlan = vc1->vlan;
    VLANClientState *vc;

    for(vc = vlan->first_client; vc != NULL; vc = vc->next) {
        if (vc != vc1) {
            if (vc->fd_can_read && vc->fd_can_read(vc->opaque))
                return 1;
        }
    }
    return 0;
}

void qemu_send_packet(VLANClientState *vc1, const uint8_t *buf, int size)
{
    VLANState *vlan = vc1->vlan;
    VLANClientState *vc;

#ifdef DEBUG_NET
    printf("vlan %d send:\n", vlan->id);
    hex_dump(stdout, buf, size);
#endif
    for(vc = vlan->first_client; vc != NULL; vc = vc->next) {
        if (vc != vc1) {
            vc->fd_read(vc->opaque, buf, size);
        }
    }
}

#if defined(CONFIG_SLIRP)

/* slirp network adapter */

static int slirp_inited;
static VLANClientState *slirp_vc;

int slirp_can_output(void)
{
    return !slirp_vc || qemu_can_send_packet(slirp_vc);
}

void slirp_output(const uint8_t *pkt, int pkt_len)
{
#ifdef DEBUG_SLIRP
    printf("slirp output:\n");
    hex_dump(stdout, pkt, pkt_len);
#endif
    if (!slirp_vc)
        return;
    qemu_send_packet(slirp_vc, pkt, pkt_len);
}

static void slirp_receive(void *opaque, const uint8_t *buf, int size)
{
#ifdef DEBUG_SLIRP
    printf("slirp input:\n");
    hex_dump(stdout, buf, size);
#endif
    slirp_input(buf, size);
}

static int net_slirp_init(VLANState *vlan)
{
    if (!slirp_inited) {
        slirp_inited = 1;
        slirp_init();
    }
    slirp_vc = qemu_new_vlan_client(vlan,
                                    slirp_receive, NULL, NULL);
    snprintf(slirp_vc->info_str, sizeof(slirp_vc->info_str), "user redirector");
    return 0;
}

static void net_slirp_redir(const char *redir_str)
{
    int is_udp;
    char buf[256], *r;
    const char *p;
    struct in_addr guest_addr;
    int host_port, guest_port;

    if (!slirp_inited) {
        slirp_inited = 1;
        slirp_init();
    }

    p = redir_str;
    if (get_str_sep(buf, sizeof(buf), &p, ':') < 0)
        goto fail;
    if (!strcmp(buf, "tcp")) {
        is_udp = 0;
    } else if (!strcmp(buf, "udp")) {
        is_udp = 1;
    } else {
        goto fail;
    }

    if (get_str_sep(buf, sizeof(buf), &p, ':') < 0)
        goto fail;
    host_port = strtol(buf, &r, 0);
    if (r == buf)
        goto fail;

    if (get_str_sep(buf, sizeof(buf), &p, ':') < 0)
        goto fail;
    if (buf[0] == '\0') {
        pstrcpy(buf, sizeof(buf), "10.0.2.15");
    }
    if (!inet_aton(buf, &guest_addr))
        goto fail;

    guest_port = strtol(p, &r, 0);
    if (r == p)
        goto fail;

    if (slirp_redir(is_udp, host_port, guest_addr, guest_port) < 0) {
        fprintf(stderr, "qemu: could not set up redirection\n");
        exit(1);
    }
    return;
 fail:
    fprintf(stderr, "qemu: syntax: -redir [tcp|udp]:host-port:[guest-host]:guest-port\n");
    exit(1);
}

#ifndef _WIN32

static char smb_dir[1024];

static void erase_dir(char *dir_name)
{
    DIR *d;
    struct dirent *de;
    char filename[1024];

    /* erase all the files in the directory */
    if ((d = opendir(dir_name)) != 0) {
        for(;;) {
            de = readdir(d);
            if (!de)
                break;
            if (strcmp(de->d_name, ".") != 0 &&
                strcmp(de->d_name, "..") != 0) {
                snprintf(filename, sizeof(filename), "%s/%s",
                         smb_dir, de->d_name);
                if (unlink(filename) != 0)  /* is it a directory? */
                    erase_dir(filename);
            }
        }
        closedir(d);
        rmdir(dir_name);
    }
}

/* automatic user mode samba server configuration */
static void smb_exit(void)
{
    erase_dir(smb_dir);
}

/* automatic user mode samba server configuration */
static void net_slirp_smb(const char *exported_dir)
{
    char smb_conf[1024];
    char smb_cmdline[1024];
    FILE *f;

    if (!slirp_inited) {
        slirp_inited = 1;
        slirp_init();
    }

    /* XXX: better tmp dir construction */
    snprintf(smb_dir, sizeof(smb_dir), "/tmp/qemu-smb.%d", getpid());
    if (mkdir(smb_dir, 0700) < 0) {
        fprintf(stderr, "qemu: could not create samba server dir '%s'\n", smb_dir);
        exit(1);
    }
    snprintf(smb_conf, sizeof(smb_conf), "%s/%s", smb_dir, "smb.conf");

    f = fopen(smb_conf, "w");
    if (!f) {
        fprintf(stderr, "qemu: could not create samba server configuration file '%s'\n", smb_conf);
        exit(1);
    }
    fprintf(f,
            "[global]\n"
            "private dir=%s\n"
            "smb ports=0\n"
            "socket address=127.0.0.1\n"
            "pid directory=%s\n"
            "lock directory=%s\n"
            "log file=%s/log.smbd\n"
            "smb passwd file=%s/smbpasswd\n"
            "security = share\n"
            "[qemu]\n"
            "path=%s\n"
            "read only=no\n"
            "guest ok=yes\n",
            smb_dir,
            smb_dir,
            smb_dir,
            smb_dir,
            smb_dir,
            exported_dir
            );
    fclose(f);
    atexit(smb_exit);

    snprintf(smb_cmdline, sizeof(smb_cmdline), "%s -s %s",
             SMBD_COMMAND, smb_conf);

    slirp_add_exec(0, smb_cmdline, 4, 139);
}

#endif /* !defined(_WIN32) */
void do_info_slirp(void)
{
    slirp_stats();
}

#endif /* CONFIG_SLIRP */

#if !defined(_WIN32)

typedef struct TAPState {
    VLANClientState *vc;
    int fd;
    char down_script[1024];
} TAPState;

static void tap_receive(void *opaque, const uint8_t *buf, int size)
{
    TAPState *s = opaque;
    int ret;
    for(;;) {
        ret = write(s->fd, buf, size);
        if (ret < 0 && (errno == EINTR || errno == EAGAIN)) {
        } else {
            break;
        }
    }
}

static void tap_send(void *opaque)
{
    TAPState *s = opaque;
    uint8_t buf[4096];
    int size;

#ifdef __sun__
    struct strbuf sbuf;
    int f = 0;
    sbuf.maxlen = sizeof(buf);
    sbuf.buf = buf;
    size = getmsg(s->fd, NULL, &sbuf, &f) >=0 ? sbuf.len : -1;
#else
    size = read(s->fd, buf, sizeof(buf));
#endif
    if (size > 0) {
        qemu_send_packet(s->vc, buf, size);
    }
}

/* fd support */

static TAPState *net_tap_fd_init(VLANState *vlan, int fd)
{
    TAPState *s;

    s = qemu_mallocz(sizeof(TAPState));
    if (!s)
        return NULL;
    s->fd = fd;
    s->vc = qemu_new_vlan_client(vlan, tap_receive, NULL, s);
    qemu_set_fd_handler(s->fd, tap_send, NULL, s);
    snprintf(s->vc->info_str, sizeof(s->vc->info_str), "tap: fd=%d", fd);
    return s;
}

#if defined (_BSD) || defined (__FreeBSD_kernel__)
static int tap_open(char *ifname, int ifname_size)
{
    int fd;
    char *dev;
    struct stat s;

    TFR(fd = open("/dev/tap", O_RDWR));
    if (fd < 0) {
        fprintf(stderr, "warning: could not open /dev/tap: no virtual network emulation\n");
        return -1;
    }

    fstat(fd, &s);
    dev = devname(s.st_rdev, S_IFCHR);
    pstrcpy(ifname, ifname_size, dev);

    fcntl(fd, F_SETFL, O_NONBLOCK);
    return fd;
}
#elif defined(__sun__)
#define TUNNEWPPA       (('T'<<16) | 0x0001)
/*
 * Allocate TAP device, returns opened fd.
 * Stores dev name in the first arg(must be large enough).
 */
int tap_alloc(char *dev, size_t dev_size)
{
    int tap_fd, if_fd, ppa = -1;
    static int ip_fd = 0;
    char *ptr;

    static int arp_fd = 0;
    int ip_muxid, arp_muxid;
    struct strioctl  strioc_if, strioc_ppa;
    int link_type = I_PLINK;;
    struct lifreq ifr;
    char actual_name[32] = "";

    memset(&ifr, 0x0, sizeof(ifr));

    if( *dev ){
       ptr = dev;
       while( *ptr && !isdigit((int)*ptr) ) ptr++;
       ppa = atoi(ptr);
    }

    /* Check if IP device was opened */
    if( ip_fd )
       close(ip_fd);

    TFR(ip_fd = open("/dev/udp", O_RDWR, 0));
    if (ip_fd < 0) {
       syslog(LOG_ERR, "Can't open /dev/ip (actually /dev/udp)");
       return -1;
    }

    TFR(tap_fd = open("/dev/tap", O_RDWR, 0));
    if (tap_fd < 0) {
       syslog(LOG_ERR, "Can't open /dev/tap");
       return -1;
    }

    /* Assign a new PPA and get its unit number. */
    strioc_ppa.ic_cmd = TUNNEWPPA;
    strioc_ppa.ic_timout = 0;
    strioc_ppa.ic_len = sizeof(ppa);
    strioc_ppa.ic_dp = (char *)&ppa;
    if ((ppa = ioctl (tap_fd, I_STR, &strioc_ppa)) < 0)
       syslog (LOG_ERR, "Can't assign new interface");

    TFR(if_fd = open("/dev/tap", O_RDWR, 0));
    if (if_fd < 0) {
       syslog(LOG_ERR, "Can't open /dev/tap (2)");
       return -1;
    }
    if(ioctl(if_fd, I_PUSH, "ip") < 0){
       syslog(LOG_ERR, "Can't push IP module");
       return -1;
    }

    if (ioctl(if_fd, SIOCGLIFFLAGS, &ifr) < 0)
	syslog(LOG_ERR, "Can't get flags\n");

    snprintf (actual_name, 32, "tap%d", ppa);
    strncpy (ifr.lifr_name, actual_name, sizeof (ifr.lifr_name));

    ifr.lifr_ppa = ppa;
    /* Assign ppa according to the unit number returned by tun device */

    if (ioctl (if_fd, SIOCSLIFNAME, &ifr) < 0)
        syslog (LOG_ERR, "Can't set PPA %d", ppa);
    if (ioctl(if_fd, SIOCGLIFFLAGS, &ifr) <0)
        syslog (LOG_ERR, "Can't get flags\n");
    /* Push arp module to if_fd */
    if (ioctl (if_fd, I_PUSH, "arp") < 0)
        syslog (LOG_ERR, "Can't push ARP module (2)");

    /* Push arp module to ip_fd */
    if (ioctl (ip_fd, I_POP, NULL) < 0)
        syslog (LOG_ERR, "I_POP failed\n");
    if (ioctl (ip_fd, I_PUSH, "arp") < 0)
        syslog (LOG_ERR, "Can't push ARP module (3)\n");
    /* Open arp_fd */
    TFR(arp_fd = open ("/dev/tap", O_RDWR, 0));
    if (arp_fd < 0)
       syslog (LOG_ERR, "Can't open %s\n", "/dev/tap");

    /* Set ifname to arp */
    strioc_if.ic_cmd = SIOCSLIFNAME;
    strioc_if.ic_timout = 0;
    strioc_if.ic_len = sizeof(ifr);
    strioc_if.ic_dp = (char *)&ifr;
    if (ioctl(arp_fd, I_STR, &strioc_if) < 0){
        syslog (LOG_ERR, "Can't set ifname to arp\n");
    }

    if((ip_muxid = ioctl(ip_fd, I_LINK, if_fd)) < 0){
       syslog(LOG_ERR, "Can't link TAP device to IP");
       return -1;
    }

    if ((arp_muxid = ioctl (ip_fd, link_type, arp_fd)) < 0)
        syslog (LOG_ERR, "Can't link TAP device to ARP");

    close (if_fd);

    memset(&ifr, 0x0, sizeof(ifr));
    strncpy (ifr.lifr_name, actual_name, sizeof (ifr.lifr_name));
    ifr.lifr_ip_muxid  = ip_muxid;
    ifr.lifr_arp_muxid = arp_muxid;

    if (ioctl (ip_fd, SIOCSLIFMUXID, &ifr) < 0)
    {
      ioctl (ip_fd, I_PUNLINK , arp_muxid);
      ioctl (ip_fd, I_PUNLINK, ip_muxid);
      syslog (LOG_ERR, "Can't set multiplexor id");
    }

    snprintf(dev, dev_size, "tap%d", ppa);
    return tap_fd;
}

static int tap_open(char *ifname, int ifname_size)
{
    char  dev[10]="";
    int fd;
    if( (fd = tap_alloc(dev, sizeof(dev))) < 0 ){
       fprintf(stderr, "Cannot allocate TAP device\n");
       return -1;
    }
    pstrcpy(ifname, ifname_size, dev);
    fcntl(fd, F_SETFL, O_NONBLOCK);
    return fd;
}
#else
static int tap_open(char *ifname, int ifname_size)
{
    struct ifreq ifr;
    int fd, ret;

    TFR(fd = open("/dev/net/tun", O_RDWR));
    if (fd < 0) {
        fprintf(stderr, "warning: could not open /dev/net/tun: no virtual network emulation\n");
        return -1;
    }
    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
    if (ifname[0] != '\0')
        pstrcpy(ifr.ifr_name, IFNAMSIZ, ifname);
    else
        pstrcpy(ifr.ifr_name, IFNAMSIZ, "tap%d");
    ret = ioctl(fd, TUNSETIFF, (void *) &ifr);
    if (ret != 0) {
        fprintf(stderr, "warning: could not configure /dev/net/tun: no virtual network emulation\n");
        close(fd);
        return -1;
    }
    pstrcpy(ifname, ifname_size, ifr.ifr_name);
    fcntl(fd, F_SETFL, O_NONBLOCK);
    return fd;
}
#endif

static int launch_script(const char *setup_script, const char *ifname, int fd)
{
    int pid, status;
    char *args[3];
    char **parg;

        /* try to launch network script */
        pid = fork();
        if (pid >= 0) {
            if (pid == 0) {
                int open_max = sysconf (_SC_OPEN_MAX), i;
                for (i = 0; i < open_max; i++)
                    if (i != STDIN_FILENO &&
                        i != STDOUT_FILENO &&
                        i != STDERR_FILENO &&
                        i != fd)
                        close(i);

                parg = args;
                *parg++ = (char *)setup_script;
                *parg++ = (char *)ifname;
                *parg++ = NULL;
                execv(setup_script, args);
                _exit(1);
            }
            while (waitpid(pid, &status, 0) != pid);
            if (!WIFEXITED(status) ||
                WEXITSTATUS(status) != 0) {
                fprintf(stderr, "%s: could not launch network script\n",
                        setup_script);
                return -1;
            }
        }
    return 0;
}

static int net_tap_init(VLANState *vlan, const char *ifname1,
                        const char *setup_script, const char *down_script)
{
    TAPState *s;
    int fd;
    char ifname[128];

    if (ifname1 != NULL)
        pstrcpy(ifname, sizeof(ifname), ifname1);
    else
        ifname[0] = '\0';
    TFR(fd = tap_open(ifname, sizeof(ifname)));
    if (fd < 0)
        return -1;

    if (!setup_script || !strcmp(setup_script, "no"))
        setup_script = "";
    if (setup_script[0] != '\0') {
	if (launch_script(setup_script, ifname, fd))
	    return -1;
    }
    s = net_tap_fd_init(vlan, fd);
    if (!s)
        return -1;
    snprintf(s->vc->info_str, sizeof(s->vc->info_str),
             "tap: ifname=%s setup_script=%s", ifname, setup_script);
    if (down_script && strcmp(down_script, "no"))
        snprintf(s->down_script, sizeof(s->down_script), "%s", down_script);
    return 0;
}

#endif /* !_WIN32 */

#if defined(CONFIG_VDE)
typedef struct VDEState {
    VLANClientState *vc;
    VDECONN *vde;
} VDEState;

static void vde_to_qemu(void *opaque)
{
    VDEState *s = opaque;
    uint8_t buf[4096];
    int size;

    size = vde_recv(s->vde, buf, sizeof(buf), 0);
    if (size > 0) {
        qemu_send_packet(s->vc, buf, size);
    }
}

static void vde_from_qemu(void *opaque, const uint8_t *buf, int size)
{
    VDEState *s = opaque;
    int ret;
    for(;;) {
        ret = vde_send(s->vde, buf, size, 0);
        if (ret < 0 && errno == EINTR) {
        } else {
            break;
        }
    }
}

static int net_vde_init(VLANState *vlan, const char *sock, int port,
                        const char *group, int mode)
{
    VDEState *s;
    char *init_group = strlen(group) ? (char *)group : NULL;
    char *init_sock = strlen(sock) ? (char *)sock : NULL;

    struct vde_open_args args = {
        .port = port,
        .group = init_group,
        .mode = mode,
    };

    s = qemu_mallocz(sizeof(VDEState));
    if (!s)
        return -1;
    s->vde = vde_open(init_sock, "QEMU", &args);
    if (!s->vde){
        free(s);
        return -1;
    }
    s->vc = qemu_new_vlan_client(vlan, vde_from_qemu, NULL, s);
    qemu_set_fd_handler(vde_datafd(s->vde), vde_to_qemu, NULL, s);
    snprintf(s->vc->info_str, sizeof(s->vc->info_str), "vde: sock=%s fd=%d",
             sock, vde_datafd(s->vde));
    return 0;
}
#endif

/* network connection */
typedef struct NetSocketState {
    VLANClientState *vc;
    int fd;
    int state; /* 0 = getting length, 1 = getting data */
    int index;
    int packet_len;
    uint8_t buf[4096];
    struct sockaddr_in dgram_dst; /* contains inet host and port destination iff connectionless (SOCK_DGRAM) */
} NetSocketState;

typedef struct NetSocketListenState {
    VLANState *vlan;
    int fd;
} NetSocketListenState;

/* XXX: we consider we can send the whole packet without blocking */
static void net_socket_receive(void *opaque, const uint8_t *buf, int size)
{
    NetSocketState *s = opaque;
    uint32_t len;
    len = htonl(size);

    send_all(s->fd, (const uint8_t *)&len, sizeof(len));
    send_all(s->fd, buf, size);
}

static void net_socket_receive_dgram(void *opaque, const uint8_t *buf, int size)
{
    NetSocketState *s = opaque;
    sendto(s->fd, buf, size, 0,
           (struct sockaddr *)&s->dgram_dst, sizeof(s->dgram_dst));
}

static void net_socket_send(void *opaque)
{
    NetSocketState *s = opaque;
    int l, size, err;
    uint8_t buf1[4096];
    const uint8_t *buf;

    size = recv(s->fd, buf1, sizeof(buf1), 0);
    if (size < 0) {
        err = socket_error();
        if (err != EWOULDBLOCK)
            goto eoc;
    } else if (size == 0) {
        /* end of connection */
    eoc:
        qemu_set_fd_handler(s->fd, NULL, NULL, NULL);
        closesocket(s->fd);
        return;
    }
    buf = buf1;
    while (size > 0) {
        /* reassemble a packet from the network */
        switch(s->state) {
        case 0:
            l = 4 - s->index;
            if (l > size)
                l = size;
            memcpy(s->buf + s->index, buf, l);
            buf += l;
            size -= l;
            s->index += l;
            if (s->index == 4) {
                /* got length */
                s->packet_len = ntohl(*(uint32_t *)s->buf);
                s->index = 0;
                s->state = 1;
            }
            break;
        case 1:
            l = s->packet_len - s->index;
            if (l > size)
                l = size;
            memcpy(s->buf + s->index, buf, l);
            s->index += l;
            buf += l;
            size -= l;
            if (s->index >= s->packet_len) {
                qemu_send_packet(s->vc, s->buf, s->packet_len);
                s->index = 0;
                s->state = 0;
            }
            break;
        }
    }
}

static void net_socket_send_dgram(void *opaque)
{
    NetSocketState *s = opaque;
    int size;

    size = recv(s->fd, s->buf, sizeof(s->buf), 0);
    if (size < 0)
        return;
    if (size == 0) {
        /* end of connection */
        qemu_set_fd_handler(s->fd, NULL, NULL, NULL);
        return;
    }
    qemu_send_packet(s->vc, s->buf, size);
}

static int net_socket_mcast_create(struct sockaddr_in *mcastaddr)
{
    struct ip_mreq imr;
    int fd;
    int val, ret;
    if (!IN_MULTICAST(ntohl(mcastaddr->sin_addr.s_addr))) {
	fprintf(stderr, "qemu: error: specified mcastaddr \"%s\" (0x%08x) does not contain a multicast address\n",
		inet_ntoa(mcastaddr->sin_addr),
                (int)ntohl(mcastaddr->sin_addr.s_addr));
	return -1;

    }
    fd = socket(PF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket(PF_INET, SOCK_DGRAM)");
        return -1;
    }

    val = 1;
    ret=setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
                   (const char *)&val, sizeof(val));
    if (ret < 0) {
	perror("setsockopt(SOL_SOCKET, SO_REUSEADDR)");
	goto fail;
    }

    ret = bind(fd, (struct sockaddr *)mcastaddr, sizeof(*mcastaddr));
    if (ret < 0) {
        perror("bind");
        goto fail;
    }

    /* Add host to multicast group */
    imr.imr_multiaddr = mcastaddr->sin_addr;
    imr.imr_interface.s_addr = htonl(INADDR_ANY);

    ret = setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                     (const char *)&imr, sizeof(struct ip_mreq));
    if (ret < 0) {
	perror("setsockopt(IP_ADD_MEMBERSHIP)");
	goto fail;
    }

    /* Force mcast msgs to loopback (eg. several QEMUs in same host */
    val = 1;
    ret=setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP,
                   (const char *)&val, sizeof(val));
    if (ret < 0) {
	perror("setsockopt(SOL_IP, IP_MULTICAST_LOOP)");
	goto fail;
    }

    socket_set_nonblock(fd);
    return fd;
fail:
    if (fd >= 0)
        closesocket(fd);
    return -1;
}

static NetSocketState *net_socket_fd_init_dgram(VLANState *vlan, int fd,
                                          int is_connected)
{
    struct sockaddr_in saddr;
    int newfd;
    socklen_t saddr_len;
    NetSocketState *s;

    /* fd passed: multicast: "learn" dgram_dst address from bound address and save it
     * Because this may be "shared" socket from a "master" process, datagrams would be recv()
     * by ONLY ONE process: we must "clone" this dgram socket --jjo
     */

    if (is_connected) {
	if (getsockname(fd, (struct sockaddr *) &saddr, &saddr_len) == 0) {
	    /* must be bound */
	    if (saddr.sin_addr.s_addr==0) {
		fprintf(stderr, "qemu: error: init_dgram: fd=%d unbound, cannot setup multicast dst addr\n",
			fd);
		return NULL;
	    }
	    /* clone dgram socket */
	    newfd = net_socket_mcast_create(&saddr);
	    if (newfd < 0) {
		/* error already reported by net_socket_mcast_create() */
		close(fd);
		return NULL;
	    }
	    /* clone newfd to fd, close newfd */
	    dup2(newfd, fd);
	    close(newfd);

	} else {
	    fprintf(stderr, "qemu: error: init_dgram: fd=%d failed getsockname(): %s\n",
		    fd, strerror(errno));
	    return NULL;
	}
    }

    s = qemu_mallocz(sizeof(NetSocketState));
    if (!s)
        return NULL;
    s->fd = fd;

    s->vc = qemu_new_vlan_client(vlan, net_socket_receive_dgram, NULL, s);
    qemu_set_fd_handler(s->fd, net_socket_send_dgram, NULL, s);

    /* mcast: save bound address as dst */
    if (is_connected) s->dgram_dst=saddr;

    snprintf(s->vc->info_str, sizeof(s->vc->info_str),
	    "socket: fd=%d (%s mcast=%s:%d)",
	    fd, is_connected? "cloned" : "",
	    inet_ntoa(saddr.sin_addr), ntohs(saddr.sin_port));
    return s;
}

static void net_socket_connect(void *opaque)
{
    NetSocketState *s = opaque;
    qemu_set_fd_handler(s->fd, net_socket_send, NULL, s);
}

static NetSocketState *net_socket_fd_init_stream(VLANState *vlan, int fd,
                                          int is_connected)
{
    NetSocketState *s;
    s = qemu_mallocz(sizeof(NetSocketState));
    if (!s)
        return NULL;
    s->fd = fd;
    s->vc = qemu_new_vlan_client(vlan,
                                 net_socket_receive, NULL, s);
    snprintf(s->vc->info_str, sizeof(s->vc->info_str),
             "socket: fd=%d", fd);
    if (is_connected) {
        net_socket_connect(s);
    } else {
        qemu_set_fd_handler(s->fd, NULL, net_socket_connect, s);
    }
    return s;
}

static NetSocketState *net_socket_fd_init(VLANState *vlan, int fd,
                                          int is_connected)
{
    int so_type=-1, optlen=sizeof(so_type);

    if(getsockopt(fd, SOL_SOCKET, SO_TYPE, (char *)&so_type,
        (socklen_t *)&optlen)< 0) {
	fprintf(stderr, "qemu: error: getsockopt(SO_TYPE) for fd=%d failed\n", fd);
	return NULL;
    }
    switch(so_type) {
    case SOCK_DGRAM:
        return net_socket_fd_init_dgram(vlan, fd, is_connected);
    case SOCK_STREAM:
        return net_socket_fd_init_stream(vlan, fd, is_connected);
    default:
        /* who knows ... this could be a eg. a pty, do warn and continue as stream */
        fprintf(stderr, "qemu: warning: socket type=%d for fd=%d is not SOCK_DGRAM or SOCK_STREAM\n", so_type, fd);
        return net_socket_fd_init_stream(vlan, fd, is_connected);
    }
    return NULL;
}

static void net_socket_accept(void *opaque)
{
    NetSocketListenState *s = opaque;
    NetSocketState *s1;
    struct sockaddr_in saddr;
    socklen_t len;
    int fd;

    for(;;) {
        len = sizeof(saddr);
        fd = accept(s->fd, (struct sockaddr *)&saddr, &len);
        if (fd < 0 && errno != EINTR) {
            return;
        } else if (fd >= 0) {
            break;
        }
    }
    s1 = net_socket_fd_init(s->vlan, fd, 1);
    if (!s1) {
        closesocket(fd);
    } else {
        snprintf(s1->vc->info_str, sizeof(s1->vc->info_str),
                 "socket: connection from %s:%d",
                 inet_ntoa(saddr.sin_addr), ntohs(saddr.sin_port));
    }
}

static int net_socket_listen_init(VLANState *vlan, const char *host_str)
{
    NetSocketListenState *s;
    int fd, val, ret;
    struct sockaddr_in saddr;

    if (parse_host_port(&saddr, host_str) < 0)
        return -1;

    s = qemu_mallocz(sizeof(NetSocketListenState));
    if (!s)
        return -1;

    fd = socket(PF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }
    socket_set_nonblock(fd);

    /* allow fast reuse */
    val = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&val, sizeof(val));

    ret = bind(fd, (struct sockaddr *)&saddr, sizeof(saddr));
    if (ret < 0) {
        perror("bind");
        return -1;
    }
    ret = listen(fd, 0);
    if (ret < 0) {
        perror("listen");
        return -1;
    }
    s->vlan = vlan;
    s->fd = fd;
    qemu_set_fd_handler(fd, net_socket_accept, NULL, s);
    return 0;
}

static int net_socket_connect_init(VLANState *vlan, const char *host_str)
{
    NetSocketState *s;
    int fd, connected, ret, err;
    struct sockaddr_in saddr;

    if (parse_host_port(&saddr, host_str) < 0)
        return -1;

    fd = socket(PF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }
    socket_set_nonblock(fd);

    connected = 0;
    for(;;) {
        ret = connect(fd, (struct sockaddr *)&saddr, sizeof(saddr));
        if (ret < 0) {
            err = socket_error();
            if (err == EINTR || err == EWOULDBLOCK) {
            } else if (err == EINPROGRESS) {
                break;
#ifdef _WIN32
            } else if (err == WSAEALREADY) {
                break;
#endif
            } else {
                perror("connect");
                closesocket(fd);
                return -1;
            }
        } else {
            connected = 1;
            break;
        }
    }
    s = net_socket_fd_init(vlan, fd, connected);
    if (!s)
        return -1;
    snprintf(s->vc->info_str, sizeof(s->vc->info_str),
             "socket: connect to %s:%d",
             inet_ntoa(saddr.sin_addr), ntohs(saddr.sin_port));
    return 0;
}

static int net_socket_mcast_init(VLANState *vlan, const char *host_str)
{
    NetSocketState *s;
    int fd;
    struct sockaddr_in saddr;

    if (parse_host_port(&saddr, host_str) < 0)
        return -1;


    fd = net_socket_mcast_create(&saddr);
    if (fd < 0)
	return -1;

    s = net_socket_fd_init(vlan, fd, 0);
    if (!s)
        return -1;

    s->dgram_dst = saddr;

    snprintf(s->vc->info_str, sizeof(s->vc->info_str),
             "socket: mcast=%s:%d",
             inet_ntoa(saddr.sin_addr), ntohs(saddr.sin_port));
    return 0;

}

static const char *get_opt_name(char *buf, int buf_size, const char *p)
{
    char *q;

    q = buf;
    while (*p != '\0' && *p != '=') {
        if (q && (q - buf) < buf_size - 1)
            *q++ = *p;
        p++;
    }
    if (q)
        *q = '\0';

    return p;
}

static const char *get_opt_value(char *buf, int buf_size, const char *p)
{
    char *q;

    q = buf;
    while (*p != '\0') {
        if (*p == ',') {
            if (*(p + 1) != ',')
                break;
            p++;
        }
        if (q && (q - buf) < buf_size - 1)
            *q++ = *p;
        p++;
    }
    if (q)
        *q = '\0';

    return p;
}

static int get_param_value(char *buf, int buf_size,
                           const char *tag, const char *str)
{
    const char *p;
    char option[128];

    p = str;
    for(;;) {
        p = get_opt_name(option, sizeof(option), p);
        if (*p != '=')
            break;
        p++;
        if (!strcmp(tag, option)) {
            (void)get_opt_value(buf, buf_size, p);
            return strlen(buf);
        } else {
            p = get_opt_value(NULL, 0, p);
        }
        if (*p != ',')
            break;
        p++;
    }
    return 0;
}

static int check_params(char *buf, int buf_size,
                        const char * const *params, const char *str)
{
    const char *p;
    int i;

    p = str;
    for(;;) {
        p = get_opt_name(buf, buf_size, p);
        if (*p != '=')
            return -1;
        p++;
        for(i = 0; params[i] != NULL; i++)
            if (!strcmp(params[i], buf))
                break;
        if (params[i] == NULL)
            return -1;
        p = get_opt_value(NULL, 0, p);
        if (*p != ',')
            break;
        p++;
    }
    return 0;
}

static int net_client_init(const char *device, const char *p)
{
    char buf[1024];
    int vlan_id, ret;
    VLANState *vlan;

    vlan_id = 0;
    if (get_param_value(buf, sizeof(buf), "vlan", p)) {
        vlan_id = strtol(buf, NULL, 0);
    }
    vlan = qemu_find_vlan(vlan_id);
    if (!vlan) {
        fprintf(stderr, "Could not create vlan %d\n", vlan_id);
        return -1;
    }
    if (!strcmp(device, "nic")) {
        NICInfo *nd;
        uint8_t *macaddr;

        if (nb_nics >= MAX_NICS) {
            fprintf(stderr, "Too Many NICs\n");
            return -1;
        }
        nd = &nd_table[nb_nics];
        macaddr = nd->macaddr;
        macaddr[0] = 0x52;
        macaddr[1] = 0x54;
        macaddr[2] = 0x00;
        macaddr[3] = 0x12;
        macaddr[4] = 0x34;
        macaddr[5] = 0x56 + nb_nics;

        if (get_param_value(buf, sizeof(buf), "macaddr", p)) {
            if (parse_macaddr(macaddr, buf) < 0) {
                fprintf(stderr, "invalid syntax for ethernet address\n");
                return -1;
            }
        }
        if (get_param_value(buf, sizeof(buf), "model", p)) {
            nd->model = strdup(buf);
        }
        nd->vlan = vlan;
        nb_nics++;
        vlan->nb_guest_devs++;
        ret = 0;
    } else
    if (!strcmp(device, "none")) {
        /* does nothing. It is needed to signal that no network cards
           are wanted */
        ret = 0;
    } else
#ifdef CONFIG_SLIRP
    if (!strcmp(device, "user")) {
        if (get_param_value(buf, sizeof(buf), "hostname", p)) {
            pstrcpy(slirp_hostname, sizeof(slirp_hostname), buf);
        }
        vlan->nb_host_devs++;
        ret = net_slirp_init(vlan);
    } else
#endif
#ifdef _WIN32
    if (!strcmp(device, "tap")) {
        char ifname[64];
        if (get_param_value(ifname, sizeof(ifname), "ifname", p) <= 0) {
            fprintf(stderr, "tap: no interface name\n");
            return -1;
        }
        vlan->nb_host_devs++;
        ret = tap_win32_init(vlan, ifname);
    } else
#else
    if (!strcmp(device, "tap")) {
        char ifname[64];
        char setup_script[1024], down_script[1024];
        int fd;
        vlan->nb_host_devs++;
        if (get_param_value(buf, sizeof(buf), "fd", p) > 0) {
            fd = strtol(buf, NULL, 0);
            fcntl(fd, F_SETFL, O_NONBLOCK);
            ret = -1;
            if (net_tap_fd_init(vlan, fd))
                ret = 0;
        } else {
            if (get_param_value(ifname, sizeof(ifname), "ifname", p) <= 0) {
                ifname[0] = '\0';
            }
            if (get_param_value(setup_script, sizeof(setup_script), "script", p) == 0) {
                pstrcpy(setup_script, sizeof(setup_script), DEFAULT_NETWORK_SCRIPT);
            }
            if (get_param_value(down_script, sizeof(down_script), "downscript", p) == 0) {
                pstrcpy(down_script, sizeof(down_script), DEFAULT_NETWORK_DOWN_SCRIPT);
            }
            ret = net_tap_init(vlan, ifname, setup_script, down_script);
        }
    } else
#endif
    if (!strcmp(device, "socket")) {
        if (get_param_value(buf, sizeof(buf), "fd", p) > 0) {
            int fd;
            fd = strtol(buf, NULL, 0);
            ret = -1;
            if (net_socket_fd_init(vlan, fd, 1))
                ret = 0;
        } else if (get_param_value(buf, sizeof(buf), "listen", p) > 0) {
            ret = net_socket_listen_init(vlan, buf);
        } else if (get_param_value(buf, sizeof(buf), "connect", p) > 0) {
            ret = net_socket_connect_init(vlan, buf);
        } else if (get_param_value(buf, sizeof(buf), "mcast", p) > 0) {
            ret = net_socket_mcast_init(vlan, buf);
        } else {
            fprintf(stderr, "Unknown socket options: %s\n", p);
            return -1;
        }
        vlan->nb_host_devs++;
    } else
#ifdef CONFIG_VDE
    if (!strcmp(device, "vde")) {
        char vde_sock[1024], vde_group[512];
	int vde_port, vde_mode;
        vlan->nb_host_devs++;
        if (get_param_value(vde_sock, sizeof(vde_sock), "sock", p) <= 0) {
	    vde_sock[0] = '\0';
	}
	if (get_param_value(buf, sizeof(buf), "port", p) > 0) {
	    vde_port = strtol(buf, NULL, 10);
	} else {
	    vde_port = 0;
	}
	if (get_param_value(vde_group, sizeof(vde_group), "group", p) <= 0) {
	    vde_group[0] = '\0';
	}
	if (get_param_value(buf, sizeof(buf), "mode", p) > 0) {
	    vde_mode = strtol(buf, NULL, 8);
	} else {
	    vde_mode = 0700;
	}
	ret = net_vde_init(vlan, vde_sock, vde_port, vde_group, vde_mode);
    } else
#endif
    {
        fprintf(stderr, "Unknown network device: %s\n", device);
        return -1;
    }
    if (ret < 0) {
        fprintf(stderr, "Could not initialize device '%s'\n", device);
    }

    return ret;
}

static int net_client_parse(const char *str)
{
    const char *p;
    char *q;
    char device[64];

    p = str;
    q = device;
    while (*p != '\0' && *p != ',') {
        if ((q - device) < sizeof(device) - 1)
            *q++ = *p;
        p++;
    }
    *q = '\0';
    if (*p == ',')
        p++;

    return net_client_init(device, p);
}

void do_info_network(void)
{
    VLANState *vlan;
    VLANClientState *vc;

    for(vlan = first_vlan; vlan != NULL; vlan = vlan->next) {
        term_printf("VLAN %d devices:\n", vlan->id);
        for(vc = vlan->first_client; vc != NULL; vc = vc->next)
            term_printf("  %s\n", vc->info_str);
    }
}

/***********************************************************/
/* Bluetooth support */
static int nb_hcis;
static int cur_hci;
static struct HCIInfo *hci_table[MAX_NICS];
static struct bt_vlan_s {
    struct bt_scatternet_s net;
    int id;
    struct bt_vlan_s *next;
} *first_bt_vlan;

/* find or alloc a new bluetooth "VLAN" */
static struct bt_scatternet_s *qemu_find_bt_vlan(int id)
{
    struct bt_vlan_s **pvlan, *vlan;
    for (vlan = first_bt_vlan; vlan != NULL; vlan = vlan->next) {
        if (vlan->id == id)
            return &vlan->net;
    }
    vlan = qemu_mallocz(sizeof(struct bt_vlan_s));
    vlan->id = id;
    pvlan = &first_bt_vlan;
    while (*pvlan != NULL)
        pvlan = &(*pvlan)->next;
    *pvlan = vlan;
    return &vlan->net;
}

static void null_hci_send(struct HCIInfo *hci, const uint8_t *data, int len)
{
}

static int null_hci_addr_set(struct HCIInfo *hci, const uint8_t *bd_addr)
{
    return -ENOTSUP;
}

static struct HCIInfo null_hci = {
    .cmd_send = null_hci_send,
    .sco_send = null_hci_send,
    .acl_send = null_hci_send,
    .bdaddr_set = null_hci_addr_set,
};

struct HCIInfo *qemu_next_hci(void)
{
    if (cur_hci == nb_hcis)
        return &null_hci;

    return hci_table[cur_hci++];
}

/***********************************************************/
/* QEMU Block devices */

#define HD_ALIAS "index=%d,media=disk"
#ifdef TARGET_PPC
#define CDROM_ALIAS "index=1,media=cdrom"
#else
#define CDROM_ALIAS "index=2,media=cdrom"
#endif
#define FD_ALIAS "index=%d,if=floppy"
#define PFLASH_ALIAS "if=pflash"
#define MTD_ALIAS "if=mtd"
#define SD_ALIAS "index=0,if=sd"

static int drive_add(const char *file, const char *fmt, ...)
{
    va_list ap;

    if (nb_drives_opt >= MAX_DRIVES) {
        fprintf(stderr, "qemu: too many drives\n");
        exit(1);
    }

    drives_opt[nb_drives_opt].file = file;
    va_start(ap, fmt);
    vsnprintf(drives_opt[nb_drives_opt].opt,
              sizeof(drives_opt[0].opt), fmt, ap);
    va_end(ap);

    return nb_drives_opt++;
}

int drive_get_index(BlockInterfaceType type, int bus, int unit)
{
    int index;

    /* seek interface, bus and unit */

    for (index = 0; index < nb_drives; index++)
        if (drives_table[index].type == type &&
	    drives_table[index].bus == bus &&
	    drives_table[index].unit == unit)
        return index;

    return -1;
}

int drive_get_max_bus(BlockInterfaceType type)
{
    int max_bus;
    int index;

    max_bus = -1;
    for (index = 0; index < nb_drives; index++) {
        if(drives_table[index].type == type &&
           drives_table[index].bus > max_bus)
            max_bus = drives_table[index].bus;
    }
    return max_bus;
}

static void bdrv_format_print(void *opaque, const char *name)
{
    fprintf(stderr, " %s", name);
}

static int drive_init(struct drive_opt *arg, int snapshot,
                      QEMUMachine *machine)
{
    char buf[128];
    char file[1024];
    char devname[128];
    const char *mediastr = "";
    BlockInterfaceType type;
    enum { MEDIA_DISK, MEDIA_CDROM } media;
    int bus_id, unit_id;
    int cyls, heads, secs, translation;
    BlockDriverState *bdrv;
    BlockDriver *drv = NULL;
    int max_devs;
    int index;
    int cache;
    int bdrv_flags;
    char *str = arg->opt;
    static const char * const params[] = { "bus", "unit", "if", "index",
                                           "cyls", "heads", "secs", "trans",
                                           "media", "snapshot", "file",
                                           "cache", "format", NULL };

    if (check_params(buf, sizeof(buf), params, str) < 0) {
         fprintf(stderr, "qemu: unknown parameter '%s' in '%s'\n",
                         buf, str);
         return -1;
    }

    file[0] = 0;
    cyls = heads = secs = 0;
    bus_id = 0;
    unit_id = -1;
    translation = BIOS_ATA_TRANSLATION_AUTO;
    index = -1;
    cache = 1;

    if (machine->use_scsi) {
        type = IF_SCSI;
        max_devs = MAX_SCSI_DEVS;
        pstrcpy(devname, sizeof(devname), "scsi");
    } else {
        type = IF_IDE;
        max_devs = MAX_IDE_DEVS;
        pstrcpy(devname, sizeof(devname), "ide");
    }
    media = MEDIA_DISK;

    /* extract parameters */

    if (get_param_value(buf, sizeof(buf), "bus", str)) {
        bus_id = strtol(buf, NULL, 0);
	if (bus_id < 0) {
	    fprintf(stderr, "qemu: '%s' invalid bus id\n", str);
	    return -1;
	}
    }

    if (get_param_value(buf, sizeof(buf), "unit", str)) {
        unit_id = strtol(buf, NULL, 0);
	if (unit_id < 0) {
	    fprintf(stderr, "qemu: '%s' invalid unit id\n", str);
	    return -1;
	}
    }

    if (get_param_value(buf, sizeof(buf), "if", str)) {
        pstrcpy(devname, sizeof(devname), buf);
        if (!strcmp(buf, "ide")) {
	    type = IF_IDE;
            max_devs = MAX_IDE_DEVS;
        } else if (!strcmp(buf, "scsi")) {
	    type = IF_SCSI;
            max_devs = MAX_SCSI_DEVS;
        } else if (!strcmp(buf, "floppy")) {
	    type = IF_FLOPPY;
            max_devs = 0;
        } else if (!strcmp(buf, "pflash")) {
	    type = IF_PFLASH;
            max_devs = 0;
	} else if (!strcmp(buf, "mtd")) {
	    type = IF_MTD;
            max_devs = 0;
	} else if (!strcmp(buf, "sd")) {
	    type = IF_SD;
            max_devs = 0;
	} else {
            fprintf(stderr, "qemu: '%s' unsupported bus type '%s'\n", str, buf);
            return -1;
	}
    }

    if (get_param_value(buf, sizeof(buf), "index", str)) {
        index = strtol(buf, NULL, 0);
	if (index < 0) {
	    fprintf(stderr, "qemu: '%s' invalid index\n", str);
	    return -1;
	}
    }

    if (get_param_value(buf, sizeof(buf), "cyls", str)) {
        cyls = strtol(buf, NULL, 0);
    }

    if (get_param_value(buf, sizeof(buf), "heads", str)) {
        heads = strtol(buf, NULL, 0);
    }

    if (get_param_value(buf, sizeof(buf), "secs", str)) {
        secs = strtol(buf, NULL, 0);
    }

    if (cyls || heads || secs) {
        if (cyls < 1 || cyls > 16383) {
            fprintf(stderr, "qemu: '%s' invalid physical cyls number\n", str);
	    return -1;
	}
        if (heads < 1 || heads > 16) {
            fprintf(stderr, "qemu: '%s' invalid physical heads number\n", str);
	    return -1;
	}
        if (secs < 1 || secs > 63) {
            fprintf(stderr, "qemu: '%s' invalid physical secs number\n", str);
	    return -1;
	}
    }

    if (get_param_value(buf, sizeof(buf), "trans", str)) {
        if (!cyls) {
            fprintf(stderr,
                    "qemu: '%s' trans must be used with cyls,heads and secs\n",
                    str);
            return -1;
        }
        if (!strcmp(buf, "none"))
            translation = BIOS_ATA_TRANSLATION_NONE;
        else if (!strcmp(buf, "lba"))
            translation = BIOS_ATA_TRANSLATION_LBA;
        else if (!strcmp(buf, "auto"))
            translation = BIOS_ATA_TRANSLATION_AUTO;
	else {
            fprintf(stderr, "qemu: '%s' invalid translation type\n", str);
	    return -1;
	}
    }

    if (get_param_value(buf, sizeof(buf), "media", str)) {
        if (!strcmp(buf, "disk")) {
	    media = MEDIA_DISK;
	} else if (!strcmp(buf, "cdrom")) {
            if (cyls || secs || heads) {
                fprintf(stderr,
                        "qemu: '%s' invalid physical CHS format\n", str);
	        return -1;
            }
	    media = MEDIA_CDROM;
	} else {
	    fprintf(stderr, "qemu: '%s' invalid media\n", str);
	    return -1;
	}
    }

    if (get_param_value(buf, sizeof(buf), "snapshot", str)) {
        if (!strcmp(buf, "on"))
	    snapshot = 1;
        else if (!strcmp(buf, "off"))
	    snapshot = 0;
	else {
	    fprintf(stderr, "qemu: '%s' invalid snapshot option\n", str);
	    return -1;
	}
    }

    if (get_param_value(buf, sizeof(buf), "cache", str)) {
        if (!strcmp(buf, "off"))
            cache = 0;
        else if (!strcmp(buf, "on"))
            cache = 1;
        else {
           fprintf(stderr, "qemu: invalid cache option\n");
           return -1;
        }
    }

    if (get_param_value(buf, sizeof(buf), "format", str)) {
       if (strcmp(buf, "?") == 0) {
            fprintf(stderr, "qemu: Supported formats:");
            bdrv_iterate_format(bdrv_format_print, NULL);
            fprintf(stderr, "\n");
	    return -1;
        }
        drv = bdrv_find_format(buf);
        if (!drv) {
            fprintf(stderr, "qemu: '%s' invalid format\n", buf);
            return -1;
        }
    }

    if (arg->file == NULL)
        get_param_value(file, sizeof(file), "file", str);
    else
        pstrcpy(file, sizeof(file), arg->file);

    /* compute bus and unit according index */

    if (index != -1) {
        if (bus_id != 0 || unit_id != -1) {
            fprintf(stderr,
                    "qemu: '%s' index cannot be used with bus and unit\n", str);
            return -1;
        }
        if (max_devs == 0)
        {
            unit_id = index;
            bus_id = 0;
        } else {
            unit_id = index % max_devs;
            bus_id = index / max_devs;
        }
    }

    /* if user doesn't specify a unit_id,
     * try to find the first free
     */

    if (unit_id == -1) {
       unit_id = 0;
       while (drive_get_index(type, bus_id, unit_id) != -1) {
           unit_id++;
           if (max_devs && unit_id >= max_devs) {
               unit_id -= max_devs;
               bus_id++;
           }
       }
    }

    /* check unit id */

    if (max_devs && unit_id >= max_devs) {
        fprintf(stderr, "qemu: '%s' unit %d too big (max is %d)\n",
                        str, unit_id, max_devs - 1);
        return -1;
    }

    /*
     * ignore multiple definitions
     */

    if (drive_get_index(type, bus_id, unit_id) != -1)
        return 0;

    /* init */

    if (type == IF_IDE || type == IF_SCSI)
        mediastr = (media == MEDIA_CDROM) ? "-cd" : "-hd";
    if (max_devs)
        snprintf(buf, sizeof(buf), "%s%i%s%i",
                 devname, bus_id, mediastr, unit_id);
    else
        snprintf(buf, sizeof(buf), "%s%s%i",
                 devname, mediastr, unit_id);
    bdrv = bdrv_new(buf);
    drives_table[nb_drives].bdrv = bdrv;
    drives_table[nb_drives].type = type;
    drives_table[nb_drives].bus = bus_id;
    drives_table[nb_drives].unit = unit_id;
    nb_drives++;

    switch(type) {
    case IF_IDE:
    case IF_SCSI:
        switch(media) {
	case MEDIA_DISK:
            if (cyls != 0) {
                bdrv_set_geometry_hint(bdrv, cyls, heads, secs);
                bdrv_set_translation_hint(bdrv, translation);
            }
	    break;
	case MEDIA_CDROM:
            bdrv_set_type_hint(bdrv, BDRV_TYPE_CDROM);
	    break;
	}
        break;
    case IF_SD:
        /* FIXME: This isn't really a floppy, but it's a reasonable
           approximation.  */
    case IF_FLOPPY:
        bdrv_set_type_hint(bdrv, BDRV_TYPE_FLOPPY);
        break;
    case IF_PFLASH:
    case IF_MTD:
        break;
    }
    if (!file[0])
        return 0;
    bdrv_flags = 0;
    if (snapshot)
        bdrv_flags |= BDRV_O_SNAPSHOT;
    if (!cache)
        bdrv_flags |= BDRV_O_DIRECT;
    if (bdrv_open2(bdrv, file, bdrv_flags, drv) < 0 || qemu_key_check(bdrv, file)) {
        fprintf(stderr, "qemu: could not open disk image %s\n",
                        file);
        return -1;
    }
    return 0;
}

/***********************************************************/
/* USB devices */

static USBPort *used_usb_ports;
static USBPort *free_usb_ports;

/* ??? Maybe change this to register a hub to keep track of the topology.  */
void qemu_register_usb_port(USBPort *port, void *opaque, int index,
                            usb_attachfn attach)
{
    port->opaque = opaque;
    port->index = index;
    port->attach = attach;
    port->next = free_usb_ports;
    free_usb_ports = port;
}

int usb_device_add_dev(USBDevice *dev)
{
    USBPort *port;

    /* Find a USB port to add the device to.  */
    port = free_usb_ports;
    if (!port->next) {
        USBDevice *hub;

        /* Create a new hub and chain it on.  */
        free_usb_ports = NULL;
        port->next = used_usb_ports;
        used_usb_ports = port;

        hub = usb_hub_init(VM_USB_HUB_SIZE);
        usb_attach(port, hub);
        port = free_usb_ports;
    }

    free_usb_ports = port->next;
    port->next = used_usb_ports;
    used_usb_ports = port;
    usb_attach(port, dev);
    return 0;
}

static int usb_device_add(const char *devname)
{
    const char *p;
    USBDevice *dev;

    if (!free_usb_ports)
        return -1;

    if (strstart(devname, "host:", &p)) {
        dev = usb_host_device_open(p);
    } else if (!strcmp(devname, "mouse")) {
        dev = usb_mouse_init();
    } else if (!strcmp(devname, "tablet")) {
        dev = usb_tablet_init();
    } else if (!strcmp(devname, "keyboard")) {
        dev = usb_keyboard_init();
    } else if (strstart(devname, "disk:", &p)) {
        dev = usb_msd_init(p);
    } else if (!strcmp(devname, "wacom-tablet")) {
        dev = usb_wacom_init();
    } else if (strstart(devname, "serial:", &p)) {
        dev = usb_serial_init(p);
#ifdef CONFIG_BRLAPI
    } else if (!strcmp(devname, "braille")) {
        dev = usb_baum_init();
#endif
    } else if (strstart(devname, "net:", &p)) {
        int nic = nb_nics;

        if (net_client_init("nic", p) < 0)
            return -1;
        nd_table[nic].model = "usb";
        dev = usb_net_init(&nd_table[nic]);
    } else {
        return -1;
    }
    if (!dev)
        return -1;

    return usb_device_add_dev(dev);
}

int usb_device_del_addr(int bus_num, int addr)
{
    USBPort *port;
    USBPort **lastp;
    USBDevice *dev;

    if (!used_usb_ports)
        return -1;

    if (bus_num != 0)
        return -1;

    lastp = &used_usb_ports;
    port = used_usb_ports;
    while (port && port->dev->addr != addr) {
        lastp = &port->next;
        port = port->next;
    }

    if (!port)
        return -1;

    dev = port->dev;
    *lastp = port->next;
    usb_attach(port, NULL);
    dev->handle_destroy(dev);
    port->next = free_usb_ports;
    free_usb_ports = port;
    return 0;
}

static int usb_device_del(const char *devname)
{
    int bus_num, addr;
    const char *p;

    if (strstart(devname, "host:", &p))
        return usb_host_device_close(p);

    if (!used_usb_ports)
        return -1;

    p = strchr(devname, '.');
    if (!p)
        return -1;
    bus_num = strtoul(devname, NULL, 0);
    addr = strtoul(p + 1, NULL, 0);

    return usb_device_del_addr(bus_num, addr);
}

void do_usb_add(const char *devname)
{
    usb_device_add(devname);
}

void do_usb_del(const char *devname)
{
    usb_device_del(devname);
}

void usb_info(void)
{
    USBDevice *dev;
    USBPort *port;
    const char *speed_str;

    if (!usb_enabled) {
        term_printf("USB support not enabled\n");
        return;
    }

    for (port = used_usb_ports; port; port = port->next) {
        dev = port->dev;
        if (!dev)
            continue;
        switch(dev->speed) {
        case USB_SPEED_LOW:
            speed_str = "1.5";
            break;
        case USB_SPEED_FULL:
            speed_str = "12";
            break;
        case USB_SPEED_HIGH:
            speed_str = "480";
            break;
        default:
            speed_str = "?";
            break;
        }
        term_printf("  Device %d.%d, Speed %s Mb/s, Product %s\n",
                    0, dev->addr, speed_str, dev->devname);
    }
}

/***********************************************************/
/* PCMCIA/Cardbus */

static struct pcmcia_socket_entry_s {
    struct pcmcia_socket_s *socket;
    struct pcmcia_socket_entry_s *next;
} *pcmcia_sockets = 0;

void pcmcia_socket_register(struct pcmcia_socket_s *socket)
{
    struct pcmcia_socket_entry_s *entry;

    entry = qemu_malloc(sizeof(struct pcmcia_socket_entry_s));
    entry->socket = socket;
    entry->next = pcmcia_sockets;
    pcmcia_sockets = entry;
}

void pcmcia_socket_unregister(struct pcmcia_socket_s *socket)
{
    struct pcmcia_socket_entry_s *entry, **ptr;

    ptr = &pcmcia_sockets;
    for (entry = *ptr; entry; ptr = &entry->next, entry = *ptr)
        if (entry->socket == socket) {
            *ptr = entry->next;
            qemu_free(entry);
        }
}

void pcmcia_info(void)
{
    struct pcmcia_socket_entry_s *iter;
    if (!pcmcia_sockets)
        term_printf("No PCMCIA sockets\n");

    for (iter = pcmcia_sockets; iter; iter = iter->next)
        term_printf("%s: %s\n", iter->socket->slot_string,
                    iter->socket->attached ? iter->socket->card_string :
                    "Empty");
}

/***********************************************************/
/* dumb display */

static void dumb_update(DisplayState *ds, int x, int y, int w, int h)
{
}

static void dumb_resize(DisplayState *ds, int w, int h)
{
}

static void dumb_refresh(DisplayState *ds)
{
#if defined(CONFIG_SDL)
    vga_hw_update();
#endif
}

static void dumb_display_init(DisplayState *ds)
{
    ds->data = NULL;
    ds->linesize = 0;
    ds->depth = 0;
    ds->dpy_update = dumb_update;
    ds->dpy_resize = dumb_resize;
    ds->dpy_refresh = dumb_refresh;
    ds->gui_timer_interval = 500;
    ds->idle = 1;
}

/***********************************************************/
/* I/O handling */

#define MAX_IO_HANDLERS 64

typedef struct IOHandlerRecord {
    int fd;
    IOCanRWHandler *fd_read_poll;
    IOHandler *fd_read;
    IOHandler *fd_write;
    int deleted;
    void *opaque;
    /* temporary data */
    struct pollfd *ufd;
    struct IOHandlerRecord *next;
} IOHandlerRecord;

static IOHandlerRecord *first_io_handler;

/* XXX: fd_read_poll should be suppressed, but an API change is
   necessary in the character devices to suppress fd_can_read(). */
int qemu_set_fd_handler2(int fd,
                         IOCanRWHandler *fd_read_poll,
                         IOHandler *fd_read,
                         IOHandler *fd_write,
                         void *opaque)
{
    IOHandlerRecord **pioh, *ioh;

    if (!fd_read && !fd_write) {
        pioh = &first_io_handler;
        for(;;) {
            ioh = *pioh;
            if (ioh == NULL)
                break;
            if (ioh->fd == fd) {
                ioh->deleted = 1;
                break;
            }
            pioh = &ioh->next;
        }
    } else {
        for(ioh = first_io_handler; ioh != NULL; ioh = ioh->next) {
            if (ioh->fd == fd)
                goto found;
        }
        ioh = qemu_mallocz(sizeof(IOHandlerRecord));
        if (!ioh)
            return -1;
        ioh->next = first_io_handler;
        first_io_handler = ioh;
    found:
        ioh->fd = fd;
        ioh->fd_read_poll = fd_read_poll;
        ioh->fd_read = fd_read;
        ioh->fd_write = fd_write;
        ioh->opaque = opaque;
        ioh->deleted = 0;
    }
    return 0;
}

int qemu_set_fd_handler(int fd,
                        IOHandler *fd_read,
                        IOHandler *fd_write,
                        void *opaque)
{
    return qemu_set_fd_handler2(fd, NULL, fd_read, fd_write, opaque);
}

/***********************************************************/
/* Polling handling */

typedef struct PollingEntry {
    PollingFunc *func;
    void *opaque;
    struct PollingEntry *next;
} PollingEntry;

static PollingEntry *first_polling_entry;

int qemu_add_polling_cb(PollingFunc *func, void *opaque)
{
    PollingEntry **ppe, *pe;
    pe = qemu_mallocz(sizeof(PollingEntry));
    if (!pe)
        return -1;
    pe->func = func;
    pe->opaque = opaque;
    for(ppe = &first_polling_entry; *ppe != NULL; ppe = &(*ppe)->next);
    *ppe = pe;
    return 0;
}

void qemu_del_polling_cb(PollingFunc *func, void *opaque)
{
    PollingEntry **ppe, *pe;
    for(ppe = &first_polling_entry; *ppe != NULL; ppe = &(*ppe)->next) {
        pe = *ppe;
        if (pe->func == func && pe->opaque == opaque) {
            *ppe = pe->next;
            qemu_free(pe);
            break;
        }
    }
}

#ifdef _WIN32
/***********************************************************/
/* Wait objects support */
typedef struct WaitObjects {
    int num;
    HANDLE events[MAXIMUM_WAIT_OBJECTS + 1];
    WaitObjectFunc *func[MAXIMUM_WAIT_OBJECTS + 1];
    void *opaque[MAXIMUM_WAIT_OBJECTS + 1];
} WaitObjects;

static WaitObjects wait_objects = {0};

int qemu_add_wait_object(HANDLE handle, WaitObjectFunc *func, void *opaque)
{
    WaitObjects *w = &wait_objects;

    if (w->num >= MAXIMUM_WAIT_OBJECTS)
        return -1;
    w->events[w->num] = handle;
    w->func[w->num] = func;
    w->opaque[w->num] = opaque;
    w->num++;
    return 0;
}

void qemu_del_wait_object(HANDLE handle, WaitObjectFunc *func, void *opaque)
{
    int i, found;
    WaitObjects *w = &wait_objects;

    found = 0;
    for (i = 0; i < w->num; i++) {
        if (w->events[i] == handle)
            found = 1;
        if (found) {
            w->events[i] = w->events[i + 1];
            w->func[i] = w->func[i + 1];
            w->opaque[i] = w->opaque[i + 1];
        }
    }
    if (found)
        w->num--;
}
#endif

/***********************************************************/
/* savevm/loadvm support */

#define IO_BUF_SIZE 32768

struct QEMUFile {
    QEMUFilePutBufferFunc *put_buffer;
    QEMUFileGetBufferFunc *get_buffer;
    QEMUFileCloseFunc *close;
    QEMUFileRateLimit *rate_limit;
    void *opaque;

    int64_t buf_offset; /* start of buffer when writing, end of buffer
                           when reading */
    int buf_index;
    int buf_size; /* 0 when writing */
    uint8_t buf[IO_BUF_SIZE];
};

typedef struct QEMUFileFD
{
    int fd;
    QEMUFile *file;
} QEMUFileFD;

static void fd_put_notify(void *opaque)
{
    QEMUFileFD *s = opaque;

    /* Remove writable callback and do a put notify */
    qemu_set_fd_handler2(s->fd, NULL, NULL, NULL, NULL);
    qemu_file_put_notify(s->file);
}

static void fd_put_buffer(void *opaque, const uint8_t *buf,
                          int64_t pos, int size)
{
    QEMUFileFD *s = opaque;
    ssize_t len;

    do {
        len = write(s->fd, buf, size);
    } while (len == -1 && errno == EINTR);

    if (len == -1)
        len = -errno;

    /* When the fd becomes writable again, register a callback to do
     * a put notify */
    if (len == -EAGAIN)
        qemu_set_fd_handler2(s->fd, NULL, NULL, fd_put_notify, s);
}

static int fd_get_buffer(void *opaque, uint8_t *buf, int64_t pos, int size)
{
    QEMUFileFD *s = opaque;
    ssize_t len;

    do {
        len = read(s->fd, buf, size);
    } while (len == -1 && errno == EINTR);

    if (len == -1)
        len = -errno;

    return len;
}

static int fd_close(void *opaque)
{
    QEMUFileFD *s = opaque;
    qemu_free(s);
    return 0;
}

QEMUFile *qemu_fopen_fd(int fd)
{
    QEMUFileFD *s = qemu_mallocz(sizeof(QEMUFileFD));

    if (s == NULL)
        return NULL;

    s->fd = fd;
    s->file = qemu_fopen_ops(s, fd_put_buffer, fd_get_buffer, fd_close, NULL);
    return s->file;
}

typedef struct QEMUFileStdio
{
    FILE *outfile;
} QEMUFileStdio;

static void file_put_buffer(void *opaque, const uint8_t *buf,
                            int64_t pos, int size)
{
    QEMUFileStdio *s = opaque;
    fseek(s->outfile, pos, SEEK_SET);
    fwrite(buf, 1, size, s->outfile);
}

static int file_get_buffer(void *opaque, uint8_t *buf, int64_t pos, int size)
{
    QEMUFileStdio *s = opaque;
    fseek(s->outfile, pos, SEEK_SET);
    return fread(buf, 1, size, s->outfile);
}

static int file_close(void *opaque)
{
    QEMUFileStdio *s = opaque;
    fclose(s->outfile);
    qemu_free(s);
    return 0;
}

QEMUFile *qemu_fopen(const char *filename, const char *mode)
{
    QEMUFileStdio *s;

    s = qemu_mallocz(sizeof(QEMUFileStdio));
    if (!s)
        return NULL;

    s->outfile = fopen(filename, mode);
    if (!s->outfile)
        goto fail;

    if (!strcmp(mode, "wb"))
        return qemu_fopen_ops(s, file_put_buffer, NULL, file_close, NULL);
    else if (!strcmp(mode, "rb"))
        return qemu_fopen_ops(s, NULL, file_get_buffer, file_close, NULL);

fail:
    if (s->outfile)
        fclose(s->outfile);
    qemu_free(s);
    return NULL;
}

typedef struct QEMUFileBdrv
{
    BlockDriverState *bs;
    int64_t base_offset;
} QEMUFileBdrv;

static void bdrv_put_buffer(void *opaque, const uint8_t *buf,
                            int64_t pos, int size)
{
    QEMUFileBdrv *s = opaque;
    bdrv_pwrite(s->bs, s->base_offset + pos, buf, size);
}

static int bdrv_get_buffer(void *opaque, uint8_t *buf, int64_t pos, int size)
{
    QEMUFileBdrv *s = opaque;
    return bdrv_pread(s->bs, s->base_offset + pos, buf, size);
}

static int bdrv_fclose(void *opaque)
{
    QEMUFileBdrv *s = opaque;
    qemu_free(s);
    return 0;
}

static QEMUFile *qemu_fopen_bdrv(BlockDriverState *bs, int64_t offset, int is_writable)
{
    QEMUFileBdrv *s;

    s = qemu_mallocz(sizeof(QEMUFileBdrv));
    if (!s)
        return NULL;

    s->bs = bs;
    s->base_offset = offset;

    if (is_writable)
        return qemu_fopen_ops(s, bdrv_put_buffer, NULL, bdrv_fclose, NULL);

    return qemu_fopen_ops(s, NULL, bdrv_get_buffer, bdrv_fclose, NULL);
}

QEMUFile *qemu_fopen_ops(void *opaque, QEMUFilePutBufferFunc *put_buffer,
                         QEMUFileGetBufferFunc *get_buffer,
                         QEMUFileCloseFunc *close,
                         QEMUFileRateLimit *rate_limit)
{
    QEMUFile *f;

    f = qemu_mallocz(sizeof(QEMUFile));
    if (!f)
        return NULL;

    f->opaque = opaque;
    f->put_buffer = put_buffer;
    f->get_buffer = get_buffer;
    f->close = close;
    f->rate_limit = rate_limit;

    return f;
}

void qemu_fflush(QEMUFile *f)
{
    if (!f->put_buffer)
        return;

    if (f->buf_index > 0) {
        f->put_buffer(f->opaque, f->buf, f->buf_offset, f->buf_index);
        f->buf_offset += f->buf_index;
        f->buf_index = 0;
    }
}

static void qemu_fill_buffer(QEMUFile *f)
{
    int len;

    if (!f->get_buffer)
        return;

    len = f->get_buffer(f->opaque, f->buf, f->buf_offset, IO_BUF_SIZE);
    if (len < 0)
        len = 0;

    f->buf_index = 0;
    f->buf_size = len;
    f->buf_offset += len;
}

int qemu_fclose(QEMUFile *f)
{
    int ret = 0;
    qemu_fflush(f);
    if (f->close)
        ret = f->close(f->opaque);
    qemu_free(f);
    return ret;
}

void qemu_file_put_notify(QEMUFile *f)
{
    f->put_buffer(f->opaque, NULL, 0, 0);
}

void qemu_put_buffer(QEMUFile *f, const uint8_t *buf, int size)
{
    int l;
    while (size > 0) {
        l = IO_BUF_SIZE - f->buf_index;
        if (l > size)
            l = size;
        memcpy(f->buf + f->buf_index, buf, l);
        f->buf_index += l;
        buf += l;
        size -= l;
        if (f->buf_index >= IO_BUF_SIZE)
            qemu_fflush(f);
    }
}

void qemu_put_byte(QEMUFile *f, int v)
{
    f->buf[f->buf_index++] = v;
    if (f->buf_index >= IO_BUF_SIZE)
        qemu_fflush(f);
}

int qemu_get_buffer(QEMUFile *f, uint8_t *buf, int size1)
{
    int size, l;

    size = size1;
    while (size > 0) {
        l = f->buf_size - f->buf_index;
        if (l == 0) {
            qemu_fill_buffer(f);
            l = f->buf_size - f->buf_index;
            if (l == 0)
                break;
        }
        if (l > size)
            l = size;
        memcpy(buf, f->buf + f->buf_index, l);
        f->buf_index += l;
        buf += l;
        size -= l;
    }
    return size1 - size;
}

int qemu_get_byte(QEMUFile *f)
{
    if (f->buf_index >= f->buf_size) {
        qemu_fill_buffer(f);
        if (f->buf_index >= f->buf_size)
            return 0;
    }
    return f->buf[f->buf_index++];
}

int64_t qemu_ftell(QEMUFile *f)
{
    return f->buf_offset - f->buf_size + f->buf_index;
}

int64_t qemu_fseek(QEMUFile *f, int64_t pos, int whence)
{
    if (whence == SEEK_SET) {
        /* nothing to do */
    } else if (whence == SEEK_CUR) {
        pos += qemu_ftell(f);
    } else {
        /* SEEK_END not supported */
        return -1;
    }
    if (f->put_buffer) {
        qemu_fflush(f);
        f->buf_offset = pos;
    } else {
        f->buf_offset = pos;
        f->buf_index = 0;
        f->buf_size = 0;
    }
    return pos;
}

int qemu_file_rate_limit(QEMUFile *f)
{
    if (f->rate_limit)
        return f->rate_limit(f->opaque);

    return 0;
}

void qemu_put_be16(QEMUFile *f, unsigned int v)
{
    qemu_put_byte(f, v >> 8);
    qemu_put_byte(f, v);
}

void qemu_put_be32(QEMUFile *f, unsigned int v)
{
    qemu_put_byte(f, v >> 24);
    qemu_put_byte(f, v >> 16);
    qemu_put_byte(f, v >> 8);
    qemu_put_byte(f, v);
}

void qemu_put_be64(QEMUFile *f, uint64_t v)
{
    qemu_put_be32(f, v >> 32);
    qemu_put_be32(f, v);
}

unsigned int qemu_get_be16(QEMUFile *f)
{
    unsigned int v;
    v = qemu_get_byte(f) << 8;
    v |= qemu_get_byte(f);
    return v;
}

unsigned int qemu_get_be32(QEMUFile *f)
{
    unsigned int v;
    v = qemu_get_byte(f) << 24;
    v |= qemu_get_byte(f) << 16;
    v |= qemu_get_byte(f) << 8;
    v |= qemu_get_byte(f);
    return v;
}

uint64_t qemu_get_be64(QEMUFile *f)
{
    uint64_t v;
    v = (uint64_t)qemu_get_be32(f) << 32;
    v |= qemu_get_be32(f);
    return v;
}

typedef struct SaveStateEntry {
    char idstr[256];
    int instance_id;
    int version_id;
    int section_id;
    SaveLiveStateHandler *save_live_state;
    SaveStateHandler *save_state;
    LoadStateHandler *load_state;
    void *opaque;
    struct SaveStateEntry *next;
} SaveStateEntry;

static SaveStateEntry *first_se;

/* TODO: Individual devices generally have very little idea about the rest
   of the system, so instance_id should be removed/replaced.
   Meanwhile pass -1 as instance_id if you do not already have a clearly
   distinguishing id for all instances of your device class. */
int register_savevm_live(const char *idstr,
                         int instance_id,
                         int version_id,
                         SaveLiveStateHandler *save_live_state,
                         SaveStateHandler *save_state,
                         LoadStateHandler *load_state,
                         void *opaque)
{
    SaveStateEntry *se, **pse;
    static int global_section_id;

    se = qemu_malloc(sizeof(SaveStateEntry));
    if (!se)
        return -1;
    pstrcpy(se->idstr, sizeof(se->idstr), idstr);
    se->instance_id = (instance_id == -1) ? 0 : instance_id;
    se->version_id = version_id;
    se->section_id = global_section_id++;
    se->save_live_state = save_live_state;
    se->save_state = save_state;
    se->load_state = load_state;
    se->opaque = opaque;
    se->next = NULL;

    /* add at the end of list */
    pse = &first_se;
    while (*pse != NULL) {
        if (instance_id == -1
                && strcmp(se->idstr, (*pse)->idstr) == 0
                && se->instance_id <= (*pse)->instance_id)
            se->instance_id = (*pse)->instance_id + 1;
        pse = &(*pse)->next;
    }
    *pse = se;
    return 0;
}

int register_savevm(const char *idstr,
                    int instance_id,
                    int version_id,
                    SaveStateHandler *save_state,
                    LoadStateHandler *load_state,
                    void *opaque)
{
    return register_savevm_live(idstr, instance_id, version_id,
                                NULL, save_state, load_state, opaque);
}

#define QEMU_VM_FILE_MAGIC           0x5145564d
#define QEMU_VM_FILE_VERSION_COMPAT  0x00000002
#define QEMU_VM_FILE_VERSION         0x00000003

#define QEMU_VM_EOF                  0x00
#define QEMU_VM_SECTION_START        0x01
#define QEMU_VM_SECTION_PART         0x02
#define QEMU_VM_SECTION_END          0x03
#define QEMU_VM_SECTION_FULL         0x04

int qemu_savevm_state_begin(QEMUFile *f)
{
    SaveStateEntry *se;

    qemu_put_be32(f, QEMU_VM_FILE_MAGIC);
    qemu_put_be32(f, QEMU_VM_FILE_VERSION);

    for (se = first_se; se != NULL; se = se->next) {
        int len;

        if (se->save_live_state == NULL)
            continue;

        /* Section type */
        qemu_put_byte(f, QEMU_VM_SECTION_START);
        qemu_put_be32(f, se->section_id);

        /* ID string */
        len = strlen(se->idstr);
        qemu_put_byte(f, len);
        qemu_put_buffer(f, (uint8_t *)se->idstr, len);

        qemu_put_be32(f, se->instance_id);
        qemu_put_be32(f, se->version_id);

        se->save_live_state(f, QEMU_VM_SECTION_START, se->opaque);
    }

    return 0;
}

int qemu_savevm_state_iterate(QEMUFile *f)
{
    SaveStateEntry *se;
    int ret = 1;

    for (se = first_se; se != NULL; se = se->next) {
        if (se->save_live_state == NULL)
            continue;

        /* Section type */
        qemu_put_byte(f, QEMU_VM_SECTION_PART);
        qemu_put_be32(f, se->section_id);

        ret &= !!se->save_live_state(f, QEMU_VM_SECTION_PART, se->opaque);
    }

    if (ret)
        return 1;

    return 0;
}

int qemu_savevm_state_complete(QEMUFile *f)
{
    SaveStateEntry *se;

    for (se = first_se; se != NULL; se = se->next) {
        if (se->save_live_state == NULL)
            continue;

        /* Section type */
        qemu_put_byte(f, QEMU_VM_SECTION_END);
        qemu_put_be32(f, se->section_id);

        se->save_live_state(f, QEMU_VM_SECTION_END, se->opaque);
    }

    for(se = first_se; se != NULL; se = se->next) {
        int len;

	if (se->save_state == NULL)
	    continue;

        /* Section type */
        qemu_put_byte(f, QEMU_VM_SECTION_FULL);
        qemu_put_be32(f, se->section_id);

        /* ID string */
        len = strlen(se->idstr);
        qemu_put_byte(f, len);
        qemu_put_buffer(f, (uint8_t *)se->idstr, len);

        qemu_put_be32(f, se->instance_id);
        qemu_put_be32(f, se->version_id);

        se->save_state(f, se->opaque);
    }

    qemu_put_byte(f, QEMU_VM_EOF);

    return 0;
}

int qemu_savevm_state(QEMUFile *f)
{
    int saved_vm_running;
    int ret;

    saved_vm_running = vm_running;
    vm_stop(0);

    ret = qemu_savevm_state_begin(f);
    if (ret < 0)
        goto out;

    do {
        ret = qemu_savevm_state_iterate(f);
        if (ret < 0)
            goto out;
    } while (ret == 0);

    ret = qemu_savevm_state_complete(f);

out:
    if (saved_vm_running)
        vm_start();
    return ret;
}

static SaveStateEntry *find_se(const char *idstr, int instance_id)
{
    SaveStateEntry *se;

    for(se = first_se; se != NULL; se = se->next) {
        if (!strcmp(se->idstr, idstr) &&
            instance_id == se->instance_id)
            return se;
    }
    return NULL;
}

typedef struct LoadStateEntry {
    SaveStateEntry *se;
    int section_id;
    int version_id;
    struct LoadStateEntry *next;
} LoadStateEntry;

static int qemu_loadvm_state_v2(QEMUFile *f)
{
    SaveStateEntry *se;
    int len, ret, instance_id, record_len, version_id;
    int64_t total_len, end_pos, cur_pos;
    char idstr[256];

    total_len = qemu_get_be64(f);
    end_pos = total_len + qemu_ftell(f);
    for(;;) {
        if (qemu_ftell(f) >= end_pos)
            break;
        len = qemu_get_byte(f);
        qemu_get_buffer(f, (uint8_t *)idstr, len);
        idstr[len] = '\0';
        instance_id = qemu_get_be32(f);
        version_id = qemu_get_be32(f);
        record_len = qemu_get_be32(f);
        cur_pos = qemu_ftell(f);
        se = find_se(idstr, instance_id);
        if (!se) {
            fprintf(stderr, "qemu: warning: instance 0x%x of device '%s' not present in current VM\n",
                    instance_id, idstr);
        } else {
            ret = se->load_state(f, se->opaque, version_id);
            if (ret < 0) {
                fprintf(stderr, "qemu: warning: error while loading state for instance 0x%x of device '%s'\n",
                        instance_id, idstr);
            }
        }
        /* always seek to exact end of record */
        qemu_fseek(f, cur_pos + record_len, SEEK_SET);
    }
    return 0;
}

int qemu_loadvm_state(QEMUFile *f)
{
    LoadStateEntry *first_le = NULL;
    uint8_t section_type;
    unsigned int v;
    int ret;

    v = qemu_get_be32(f);
    if (v != QEMU_VM_FILE_MAGIC)
        return -EINVAL;

    v = qemu_get_be32(f);
    if (v == QEMU_VM_FILE_VERSION_COMPAT)
        return qemu_loadvm_state_v2(f);
    if (v != QEMU_VM_FILE_VERSION)
        return -ENOTSUP;

    while ((section_type = qemu_get_byte(f)) != QEMU_VM_EOF) {
        uint32_t instance_id, version_id, section_id;
        LoadStateEntry *le;
        SaveStateEntry *se;
        char idstr[257];
        int len;

        switch (section_type) {
        case QEMU_VM_SECTION_START:
        case QEMU_VM_SECTION_FULL:
            /* Read section start */
            section_id = qemu_get_be32(f);
            len = qemu_get_byte(f);
            qemu_get_buffer(f, (uint8_t *)idstr, len);
            idstr[len] = 0;
            instance_id = qemu_get_be32(f);
            version_id = qemu_get_be32(f);

            /* Find savevm section */
            se = find_se(idstr, instance_id);
            if (se == NULL) {
                fprintf(stderr, "Unknown savevm section or instance '%s' %d\n", idstr, instance_id);
                ret = -EINVAL;
                goto out;
            }

            /* Validate version */
            if (version_id > se->version_id) {
                fprintf(stderr, "savevm: unsupported version %d for '%s' v%d\n",
                        version_id, idstr, se->version_id);
                ret = -EINVAL;
                goto out;
            }

            /* Add entry */
            le = qemu_mallocz(sizeof(*le));
            if (le == NULL) {
                ret = -ENOMEM;
                goto out;
            }

            le->se = se;
            le->section_id = section_id;
            le->version_id = version_id;
            le->next = first_le;
            first_le = le;

            le->se->load_state(f, le->se->opaque, le->version_id);
            break;
        case QEMU_VM_SECTION_PART:
        case QEMU_VM_SECTION_END:
            section_id = qemu_get_be32(f);

            for (le = first_le; le && le->section_id != section_id; le = le->next);
            if (le == NULL) {
                fprintf(stderr, "Unknown savevm section %d\n", section_id);
                ret = -EINVAL;
                goto out;
            }

            le->se->load_state(f, le->se->opaque, le->version_id);
            break;
        default:
            fprintf(stderr, "Unknown savevm section type %d\n", section_type);
            ret = -EINVAL;
            goto out;
        }
    }

    ret = 0;

out:
    while (first_le) {
        LoadStateEntry *le = first_le;
        first_le = first_le->next;
        qemu_free(le);
    }

    return ret;
}

/* device can contain snapshots */
static int bdrv_can_snapshot(BlockDriverState *bs)
{
    return (bs &&
            !bdrv_is_removable(bs) &&
            !bdrv_is_read_only(bs));
}

/* device must be snapshots in order to have a reliable snapshot */
static int bdrv_has_snapshot(BlockDriverState *bs)
{
    return (bs &&
            !bdrv_is_removable(bs) &&
            !bdrv_is_read_only(bs));
}

static BlockDriverState *get_bs_snapshots(void)
{
    BlockDriverState *bs;
    int i;

    if (bs_snapshots)
        return bs_snapshots;
    for(i = 0; i <= nb_drives; i++) {
        bs = drives_table[i].bdrv;
        if (bdrv_can_snapshot(bs))
            goto ok;
    }
    return NULL;
 ok:
    bs_snapshots = bs;
    return bs;
}

static int bdrv_snapshot_find(BlockDriverState *bs, QEMUSnapshotInfo *sn_info,
                              const char *name)
{
    QEMUSnapshotInfo *sn_tab, *sn;
    int nb_sns, i, ret;

    ret = -ENOENT;
    nb_sns = bdrv_snapshot_list(bs, &sn_tab);
    if (nb_sns < 0)
        return ret;
    for(i = 0; i < nb_sns; i++) {
        sn = &sn_tab[i];
        if (!strcmp(sn->id_str, name) || !strcmp(sn->name, name)) {
            *sn_info = *sn;
            ret = 0;
            break;
        }
    }
    qemu_free(sn_tab);
    return ret;
}

void do_savevm(const char *name)
{
    BlockDriverState *bs, *bs1;
    QEMUSnapshotInfo sn1, *sn = &sn1, old_sn1, *old_sn = &old_sn1;
    int must_delete, ret, i;
    BlockDriverInfo bdi1, *bdi = &bdi1;
    QEMUFile *f;
    int saved_vm_running;
#ifdef _WIN32
    struct _timeb tb;
#else
    struct timeval tv;
#endif

    bs = get_bs_snapshots();
    if (!bs) {
        term_printf("No block device can accept snapshots\n");
        return;
    }

    /* ??? Should this occur after vm_stop?  */
    qemu_aio_flush();

    saved_vm_running = vm_running;
    vm_stop(0);

    must_delete = 0;
    if (name) {
        ret = bdrv_snapshot_find(bs, old_sn, name);
        if (ret >= 0) {
            must_delete = 1;
        }
    }
    memset(sn, 0, sizeof(*sn));
    if (must_delete) {
        pstrcpy(sn->name, sizeof(sn->name), old_sn->name);
        pstrcpy(sn->id_str, sizeof(sn->id_str), old_sn->id_str);
    } else {
        if (name)
            pstrcpy(sn->name, sizeof(sn->name), name);
    }

    /* fill auxiliary fields */
#ifdef _WIN32
    _ftime(&tb);
    sn->date_sec = tb.time;
    sn->date_nsec = tb.millitm * 1000000;
#else
    gettimeofday(&tv, NULL);
    sn->date_sec = tv.tv_sec;
    sn->date_nsec = tv.tv_usec * 1000;
#endif
    sn->vm_clock_nsec = qemu_get_clock(vm_clock);

    if (bdrv_get_info(bs, bdi) < 0 || bdi->vm_state_offset <= 0) {
        term_printf("Device %s does not support VM state snapshots\n",
                    bdrv_get_device_name(bs));
        goto the_end;
    }

    /* save the VM state */
    f = qemu_fopen_bdrv(bs, bdi->vm_state_offset, 1);
    if (!f) {
        term_printf("Could not open VM state file\n");
        goto the_end;
    }
    ret = qemu_savevm_state(f);
    sn->vm_state_size = qemu_ftell(f);
    qemu_fclose(f);
    if (ret < 0) {
        term_printf("Error %d while writing VM\n", ret);
        goto the_end;
    }

    /* create the snapshots */

    for(i = 0; i < nb_drives; i++) {
        bs1 = drives_table[i].bdrv;
        if (bdrv_has_snapshot(bs1)) {
            if (must_delete) {
                ret = bdrv_snapshot_delete(bs1, old_sn->id_str);
                if (ret < 0) {
                    term_printf("Error while deleting snapshot on '%s'\n",
                                bdrv_get_device_name(bs1));
                }
            }
            ret = bdrv_snapshot_create(bs1, sn);
            if (ret < 0) {
                term_printf("Error while creating snapshot on '%s'\n",
                            bdrv_get_device_name(bs1));
            }
        }
    }

 the_end:
    if (saved_vm_running)
        vm_start();
}

void do_loadvm(const char *name)
{
    BlockDriverState *bs, *bs1;
    BlockDriverInfo bdi1, *bdi = &bdi1;
    QEMUFile *f;
    int i, ret;
    int saved_vm_running;

    bs = get_bs_snapshots();
    if (!bs) {
        term_printf("No block device supports snapshots\n");
        return;
    }

    /* Flush all IO requests so they don't interfere with the new state.  */
    qemu_aio_flush();

    saved_vm_running = vm_running;
    vm_stop(0);

    for(i = 0; i <= nb_drives; i++) {
        bs1 = drives_table[i].bdrv;
        if (bdrv_has_snapshot(bs1)) {
            ret = bdrv_snapshot_goto(bs1, name);
            if (ret < 0) {
                if (bs != bs1)
                    term_printf("Warning: ");
                switch(ret) {
                case -ENOTSUP:
                    term_printf("Snapshots not supported on device '%s'\n",
                                bdrv_get_device_name(bs1));
                    break;
                case -ENOENT:
                    term_printf("Could not find snapshot '%s' on device '%s'\n",
                                name, bdrv_get_device_name(bs1));
                    break;
                default:
                    term_printf("Error %d while activating snapshot on '%s'\n",
                                ret, bdrv_get_device_name(bs1));
                    break;
                }
                /* fatal on snapshot block device */
                if (bs == bs1)
                    goto the_end;
            }
        }
    }

    if (bdrv_get_info(bs, bdi) < 0 || bdi->vm_state_offset <= 0) {
        term_printf("Device %s does not support VM state snapshots\n",
                    bdrv_get_device_name(bs));
        return;
    }

    /* restore the VM state */
    f = qemu_fopen_bdrv(bs, bdi->vm_state_offset, 0);
    if (!f) {
        term_printf("Could not open VM state file\n");
        goto the_end;
    }
    ret = qemu_loadvm_state(f);
    qemu_fclose(f);
    if (ret < 0) {
        term_printf("Error %d while loading VM state\n", ret);
    }
 the_end:
    if (saved_vm_running)
        vm_start();
}

void do_delvm(const char *name)
{
    BlockDriverState *bs, *bs1;
    int i, ret;

    bs = get_bs_snapshots();
    if (!bs) {
        term_printf("No block device supports snapshots\n");
        return;
    }

    for(i = 0; i <= nb_drives; i++) {
        bs1 = drives_table[i].bdrv;
        if (bdrv_has_snapshot(bs1)) {
            ret = bdrv_snapshot_delete(bs1, name);
            if (ret < 0) {
                if (ret == -ENOTSUP)
                    term_printf("Snapshots not supported on device '%s'\n",
                                bdrv_get_device_name(bs1));
                else
                    term_printf("Error %d while deleting snapshot on '%s'\n",
                                ret, bdrv_get_device_name(bs1));
            }
        }
    }
}

void do_info_snapshots(void)
{
    BlockDriverState *bs, *bs1;
    QEMUSnapshotInfo *sn_tab, *sn;
    int nb_sns, i;
    char buf[256];

    bs = get_bs_snapshots();
    if (!bs) {
        term_printf("No available block device supports snapshots\n");
        return;
    }
    term_printf("Snapshot devices:");
    for(i = 0; i <= nb_drives; i++) {
        bs1 = drives_table[i].bdrv;
        if (bdrv_has_snapshot(bs1)) {
            if (bs == bs1)
                term_printf(" %s", bdrv_get_device_name(bs1));
        }
    }
    term_printf("\n");

    nb_sns = bdrv_snapshot_list(bs, &sn_tab);
    if (nb_sns < 0) {
        term_printf("bdrv_snapshot_list: error %d\n", nb_sns);
        return;
    }
    term_printf("Snapshot list (from %s):\n", bdrv_get_device_name(bs));
    term_printf("%s\n", bdrv_snapshot_dump(buf, sizeof(buf), NULL));
    for(i = 0; i < nb_sns; i++) {
        sn = &sn_tab[i];
        term_printf("%s\n", bdrv_snapshot_dump(buf, sizeof(buf), sn));
    }
    qemu_free(sn_tab);
}

/***********************************************************/
/* ram save/restore */

static int ram_get_page(QEMUFile *f, uint8_t *buf, int len)
{
    int v;

    v = qemu_get_byte(f);
    switch(v) {
    case 0:
        if (qemu_get_buffer(f, buf, len) != len)
            return -EIO;
        break;
    case 1:
        v = qemu_get_byte(f);
        memset(buf, v, len);
        break;
    default:
        return -EINVAL;
    }
    return 0;
}

static int ram_load_v1(QEMUFile *f, void *opaque)
{
    int ret;
    ram_addr_t i;

    if (qemu_get_be32(f) != phys_ram_size)
        return -EINVAL;
    for(i = 0; i < phys_ram_size; i+= TARGET_PAGE_SIZE) {
        ret = ram_get_page(f, phys_ram_base + i, TARGET_PAGE_SIZE);
        if (ret)
            return ret;
    }
    return 0;
}

#define BDRV_HASH_BLOCK_SIZE 1024
#define IOBUF_SIZE 4096
#define RAM_CBLOCK_MAGIC 0xfabe

typedef struct RamDecompressState {
    z_stream zstream;
    QEMUFile *f;
    uint8_t buf[IOBUF_SIZE];
} RamDecompressState;

static int ram_decompress_open(RamDecompressState *s, QEMUFile *f)
{
    int ret;
    memset(s, 0, sizeof(*s));
    s->f = f;
    ret = inflateInit(&s->zstream);
    if (ret != Z_OK)
        return -1;
    return 0;
}

static int ram_decompress_buf(RamDecompressState *s, uint8_t *buf, int len)
{
    int ret, clen;

    s->zstream.avail_out = len;
    s->zstream.next_out = buf;
    while (s->zstream.avail_out > 0) {
        if (s->zstream.avail_in == 0) {
            if (qemu_get_be16(s->f) != RAM_CBLOCK_MAGIC)
                return -1;
            clen = qemu_get_be16(s->f);
            if (clen > IOBUF_SIZE)
                return -1;
            qemu_get_buffer(s->f, s->buf, clen);
            s->zstream.avail_in = clen;
            s->zstream.next_in = s->buf;
        }
        ret = inflate(&s->zstream, Z_PARTIAL_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            return -1;
        }
    }
    return 0;
}

static void ram_decompress_close(RamDecompressState *s)
{
    inflateEnd(&s->zstream);
}

#define RAM_SAVE_FLAG_FULL	0x01
#define RAM_SAVE_FLAG_COMPRESS	0x02
#define RAM_SAVE_FLAG_MEM_SIZE	0x04
#define RAM_SAVE_FLAG_PAGE	0x08
#define RAM_SAVE_FLAG_EOS	0x10

static int is_dup_page(uint8_t *page, uint8_t ch)
{
    uint32_t val = ch << 24 | ch << 16 | ch << 8 | ch;
    uint32_t *array = (uint32_t *)page;
    int i;

    for (i = 0; i < (TARGET_PAGE_SIZE / 4); i++) {
        if (array[i] != val)
            return 0;
    }

    return 1;
}

static int ram_save_block(QEMUFile *f)
{
    static ram_addr_t current_addr = 0;
    ram_addr_t saved_addr = current_addr;
    ram_addr_t addr = 0;
    int found = 0;

    while (addr < phys_ram_size) {
        if (cpu_physical_memory_get_dirty(current_addr, MIGRATION_DIRTY_FLAG)) {
            uint8_t ch;

            cpu_physical_memory_reset_dirty(current_addr,
                                            current_addr + TARGET_PAGE_SIZE,
                                            MIGRATION_DIRTY_FLAG);

            ch = *(phys_ram_base + current_addr);

            if (is_dup_page(phys_ram_base + current_addr, ch)) {
                qemu_put_be64(f, current_addr | RAM_SAVE_FLAG_COMPRESS);
                qemu_put_byte(f, ch);
            } else {
                qemu_put_be64(f, current_addr | RAM_SAVE_FLAG_PAGE);
                qemu_put_buffer(f, phys_ram_base + current_addr, TARGET_PAGE_SIZE);
            }

            found = 1;
            break;
        }
        addr += TARGET_PAGE_SIZE;
        current_addr = (saved_addr + addr) % phys_ram_size;
    }

    return found;
}

static ram_addr_t ram_save_threshold = 10;

static ram_addr_t ram_save_remaining(void)
{
    ram_addr_t addr;
    ram_addr_t count = 0;

    for (addr = 0; addr < phys_ram_size; addr += TARGET_PAGE_SIZE) {
        if (cpu_physical_memory_get_dirty(addr, MIGRATION_DIRTY_FLAG))
            count++;
    }

    return count;
}

static int ram_save_live(QEMUFile *f, int stage, void *opaque)
{
    ram_addr_t addr;

    if (stage == 1) {
        /* Make sure all dirty bits are set */
        for (addr = 0; addr < phys_ram_size; addr += TARGET_PAGE_SIZE) {
            if (!cpu_physical_memory_get_dirty(addr, MIGRATION_DIRTY_FLAG))
                cpu_physical_memory_set_dirty(addr);
        }
        
        /* Enable dirty memory tracking */
        cpu_physical_memory_set_dirty_tracking(1);

        qemu_put_be64(f, phys_ram_size | RAM_SAVE_FLAG_MEM_SIZE);
    }

    while (!qemu_file_rate_limit(f)) {
        int ret;

        ret = ram_save_block(f);
        if (ret == 0) /* no more blocks */
            break;
    }

    /* try transferring iterative blocks of memory */

    if (stage == 3) {
        cpu_physical_memory_set_dirty_tracking(0);

        /* flush all remaining blocks regardless of rate limiting */
        while (ram_save_block(f) != 0);
    }

    qemu_put_be64(f, RAM_SAVE_FLAG_EOS);

    return (stage == 2) && (ram_save_remaining() < ram_save_threshold);
}

static int ram_load_dead(QEMUFile *f, void *opaque)
{
    RamDecompressState s1, *s = &s1;
    uint8_t buf[10];
    ram_addr_t i;

    if (ram_decompress_open(s, f) < 0)
        return -EINVAL;
    for(i = 0; i < phys_ram_size; i+= BDRV_HASH_BLOCK_SIZE) {
        if (ram_decompress_buf(s, buf, 1) < 0) {
            fprintf(stderr, "Error while reading ram block header\n");
            goto error;
        }
        if (buf[0] == 0) {
            if (ram_decompress_buf(s, phys_ram_base + i, BDRV_HASH_BLOCK_SIZE) < 0) {
                fprintf(stderr, "Error while reading ram block address=0x%08" PRIx64, (uint64_t)i);
                goto error;
            }
        } else {
        error:
            printf("Error block header\n");
            return -EINVAL;
        }
    }
    ram_decompress_close(s);

    return 0;
}

static int ram_load(QEMUFile *f, void *opaque, int version_id)
{
    ram_addr_t addr;
    int flags;

    if (version_id == 1)
        return ram_load_v1(f, opaque);

    if (version_id == 2) {
        if (qemu_get_be32(f) != phys_ram_size)
            return -EINVAL;
        return ram_load_dead(f, opaque);
    }

    if (version_id != 3)
        return -EINVAL;

    do {
        addr = qemu_get_be64(f);

        flags = addr & ~TARGET_PAGE_MASK;
        addr &= TARGET_PAGE_MASK;

        if (flags & RAM_SAVE_FLAG_MEM_SIZE) {
            if (addr != phys_ram_size)
                return -EINVAL;
        }

        if (flags & RAM_SAVE_FLAG_FULL) {
            if (ram_load_dead(f, opaque) < 0)
                return -EINVAL;
        }
        
        if (flags & RAM_SAVE_FLAG_COMPRESS) {
            uint8_t ch = qemu_get_byte(f);
            memset(phys_ram_base + addr, ch, TARGET_PAGE_SIZE);
        } else if (flags & RAM_SAVE_FLAG_PAGE)
            qemu_get_buffer(f, phys_ram_base + addr, TARGET_PAGE_SIZE);
    } while (!(flags & RAM_SAVE_FLAG_EOS));

    return 0;
}

void qemu_service_io(void)
{
    CPUState *env = cpu_single_env;
    if (env) {
        cpu_interrupt(env, CPU_INTERRUPT_EXIT);
#ifdef USE_KQEMU
        if (env->kqemu_enabled) {
            kqemu_cpu_interrupt(env);
        }
#endif
    }
}

/***********************************************************/
/* bottom halves (can be seen as timers which expire ASAP) */

struct QEMUBH {
    QEMUBHFunc *cb;
    void *opaque;
    int scheduled;
    QEMUBH *next;
};

static QEMUBH *first_bh = NULL;

QEMUBH *qemu_bh_new(QEMUBHFunc *cb, void *opaque)
{
    QEMUBH *bh;
    bh = qemu_mallocz(sizeof(QEMUBH));
    if (!bh)
        return NULL;
    bh->cb = cb;
    bh->opaque = opaque;
    return bh;
}

int qemu_bh_poll(void)
{
    QEMUBH *bh, **pbh;
    int ret;

    ret = 0;
    for(;;) {
        pbh = &first_bh;
        bh = *pbh;
        if (!bh)
            break;
        ret = 1;
        *pbh = bh->next;
        bh->scheduled = 0;
        bh->cb(bh->opaque);
    }
    return ret;
}

void qemu_bh_schedule(QEMUBH *bh)
{
    CPUState *env = cpu_single_env;
    if (bh->scheduled)
        return;
    bh->scheduled = 1;
    bh->next = first_bh;
    first_bh = bh;

    /* stop the currently executing CPU to execute the BH ASAP */
    if (env) {
        cpu_interrupt(env, CPU_INTERRUPT_EXIT);
    }
}

void qemu_bh_cancel(QEMUBH *bh)
{
    QEMUBH **pbh;
    if (bh->scheduled) {
        pbh = &first_bh;
        while (*pbh != bh)
            pbh = &(*pbh)->next;
        *pbh = bh->next;
        bh->scheduled = 0;
    }
}

void qemu_bh_delete(QEMUBH *bh)
{
    qemu_bh_cancel(bh);
    qemu_free(bh);
}

/***********************************************************/
/* machine registration */

static QEMUMachine *first_machine = NULL;

int qemu_register_machine(QEMUMachine *m)
{
    QEMUMachine **pm;
    pm = &first_machine;
    while (*pm != NULL)
        pm = &(*pm)->next;
    m->next = NULL;
    *pm = m;
    return 0;
}

static QEMUMachine *find_machine(const char *name)
{
    QEMUMachine *m;

    for(m = first_machine; m != NULL; m = m->next) {
        if (!strcmp(m->name, name))
            return m;
    }
    return NULL;
}

/***********************************************************/
/* main execution loop */

static void gui_update(void *opaque)
{
    DisplayState *ds = opaque;
    ds->dpy_refresh(ds);
    qemu_mod_timer(ds->gui_timer,
        (ds->gui_timer_interval ?
	    ds->gui_timer_interval :
	    GUI_REFRESH_INTERVAL)
	+ qemu_get_clock(rt_clock));
}

struct vm_change_state_entry {
    VMChangeStateHandler *cb;
    void *opaque;
    LIST_ENTRY (vm_change_state_entry) entries;
};

static LIST_HEAD(vm_change_state_head, vm_change_state_entry) vm_change_state_head;

VMChangeStateEntry *qemu_add_vm_change_state_handler(VMChangeStateHandler *cb,
                                                     void *opaque)
{
    VMChangeStateEntry *e;

    e = qemu_mallocz(sizeof (*e));
    if (!e)
        return NULL;

    e->cb = cb;
    e->opaque = opaque;
    LIST_INSERT_HEAD(&vm_change_state_head, e, entries);
    return e;
}

void qemu_del_vm_change_state_handler(VMChangeStateEntry *e)
{
    LIST_REMOVE (e, entries);
    qemu_free (e);
}

static void vm_state_notify(int running)
{
    VMChangeStateEntry *e;

    for (e = vm_change_state_head.lh_first; e; e = e->entries.le_next) {
        e->cb(e->opaque, running);
    }
}

/* XXX: support several handlers */
static VMStopHandler *vm_stop_cb;
static void *vm_stop_opaque;

int qemu_add_vm_stop_handler(VMStopHandler *cb, void *opaque)
{
    vm_stop_cb = cb;
    vm_stop_opaque = opaque;
    return 0;
}

void qemu_del_vm_stop_handler(VMStopHandler *cb, void *opaque)
{
    vm_stop_cb = NULL;
}

void vm_start(void)
{
    if (!vm_running) {
        cpu_enable_ticks();
        vm_running = 1;
        vm_state_notify(1);
        qemu_rearm_alarm_timer(alarm_timer);
    }
}

void vm_stop(int reason)
{
    if (vm_running) {
        cpu_disable_ticks();
        vm_running = 0;
        if (reason != 0) {
            if (vm_stop_cb) {
                vm_stop_cb(vm_stop_opaque, reason);
            }
        }
        vm_state_notify(0);
    }
}

/* reset/shutdown handler */

typedef struct QEMUResetEntry {
    QEMUResetHandler *func;
    void *opaque;
    struct QEMUResetEntry *next;
} QEMUResetEntry;

static QEMUResetEntry *first_reset_entry;
static int reset_requested;
static int shutdown_requested;
static int powerdown_requested;

int qemu_shutdown_requested(void)
{
    int r = shutdown_requested;
    shutdown_requested = 0;
    return r;
}

int qemu_reset_requested(void)
{
    int r = reset_requested;
    reset_requested = 0;
    return r;
}

int qemu_powerdown_requested(void)
{
    int r = powerdown_requested;
    powerdown_requested = 0;
    return r;
}

void qemu_register_reset(QEMUResetHandler *func, void *opaque)
{
    QEMUResetEntry **pre, *re;

    pre = &first_reset_entry;
    while (*pre != NULL)
        pre = &(*pre)->next;
    re = qemu_mallocz(sizeof(QEMUResetEntry));
    re->func = func;
    re->opaque = opaque;
    re->next = NULL;
    *pre = re;
}

void qemu_system_reset(void)
{
    QEMUResetEntry *re;

    /* reset all devices */
    for(re = first_reset_entry; re != NULL; re = re->next) {
        re->func(re->opaque);
    }
}

void qemu_system_reset_request(void)
{
    if (no_reboot) {
        shutdown_requested = 1;
    } else {
        reset_requested = 1;
    }
    if (cpu_single_env)
        cpu_interrupt(cpu_single_env, CPU_INTERRUPT_EXIT);
}

void qemu_system_shutdown_request(void)
{
    shutdown_requested = 1;
    if (cpu_single_env)
        cpu_interrupt(cpu_single_env, CPU_INTERRUPT_EXIT);
}

void qemu_system_powerdown_request(void)
{
    powerdown_requested = 1;
    if (cpu_single_env)
        cpu_interrupt(cpu_single_env, CPU_INTERRUPT_EXIT);
}

void main_loop_wait(int timeout)
{
    IOHandlerRecord *ioh;
    fd_set rfds, wfds, xfds;
    int ret, nfds;
#ifdef _WIN32
    int ret2, i;
#endif
    struct timeval tv;
    PollingEntry *pe;


    /* XXX: need to suppress polling by better using win32 events */
    ret = 0;
    for(pe = first_polling_entry; pe != NULL; pe = pe->next) {
        ret |= pe->func(pe->opaque);
    }
#ifdef _WIN32
    if (ret == 0) {
        int err;
        WaitObjects *w = &wait_objects;

        ret = WaitForMultipleObjects(w->num, w->events, FALSE, timeout);
        if (WAIT_OBJECT_0 + 0 <= ret && ret <= WAIT_OBJECT_0 + w->num - 1) {
            if (w->func[ret - WAIT_OBJECT_0])
                w->func[ret - WAIT_OBJECT_0](w->opaque[ret - WAIT_OBJECT_0]);

            /* Check for additional signaled events */
            for(i = (ret - WAIT_OBJECT_0 + 1); i < w->num; i++) {

                /* Check if event is signaled */
                ret2 = WaitForSingleObject(w->events[i], 0);
                if(ret2 == WAIT_OBJECT_0) {
                    if (w->func[i])
                        w->func[i](w->opaque[i]);
                } else if (ret2 == WAIT_TIMEOUT) {
                } else {
                    err = GetLastError();
                    fprintf(stderr, "WaitForSingleObject error %d %d\n", i, err);
                }
            }
        } else if (ret == WAIT_TIMEOUT) {
        } else {
            err = GetLastError();
            fprintf(stderr, "WaitForMultipleObjects error %d %d\n", ret, err);
        }
    }
#endif
    /* poll any events */
    /* XXX: separate device handlers from system ones */
    nfds = -1;
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    FD_ZERO(&xfds);
    for(ioh = first_io_handler; ioh != NULL; ioh = ioh->next) {
        if (ioh->deleted)
            continue;
        if (ioh->fd_read &&
            (!ioh->fd_read_poll ||
             ioh->fd_read_poll(ioh->opaque) != 0)) {
            FD_SET(ioh->fd, &rfds);
            if (ioh->fd > nfds)
                nfds = ioh->fd;
        }
        if (ioh->fd_write) {
            FD_SET(ioh->fd, &wfds);
            if (ioh->fd > nfds)
                nfds = ioh->fd;
        }
    }

    tv.tv_sec = 0;
#ifdef _WIN32
    tv.tv_usec = 0;
#else
    tv.tv_usec = timeout * 1000;
#endif
#if defined(CONFIG_SLIRP)
    if (slirp_inited) {
        slirp_select_fill(&nfds, &rfds, &wfds, &xfds);
    }
#endif
    ret = select(nfds + 1, &rfds, &wfds, &xfds, &tv);
    if (ret > 0) {
        IOHandlerRecord **pioh;

        for(ioh = first_io_handler; ioh != NULL; ioh = ioh->next) {
            if (!ioh->deleted && ioh->fd_read && FD_ISSET(ioh->fd, &rfds)) {
                ioh->fd_read(ioh->opaque);
            }
            if (!ioh->deleted && ioh->fd_write && FD_ISSET(ioh->fd, &wfds)) {
                ioh->fd_write(ioh->opaque);
            }
        }

	/* remove deleted IO handlers */
	pioh = &first_io_handler;
	while (*pioh) {
            ioh = *pioh;
            if (ioh->deleted) {
                *pioh = ioh->next;
                qemu_free(ioh);
            } else
                pioh = &ioh->next;
        }
    }
#if defined(CONFIG_SLIRP)
    if (slirp_inited) {
        if (ret < 0) {
            FD_ZERO(&rfds);
            FD_ZERO(&wfds);
            FD_ZERO(&xfds);
        }
        slirp_select_poll(&rfds, &wfds, &xfds);
    }
#endif

    if (vm_running) {
        if (likely(!(cur_cpu->singlestep_enabled & SSTEP_NOTIMER)))
        qemu_run_timers(&active_timers[QEMU_TIMER_VIRTUAL],
                        qemu_get_clock(vm_clock));
        /* run dma transfers, if any */
        DMA_run();
    }

    /* real time timers */
    qemu_run_timers(&active_timers[QEMU_TIMER_REALTIME],
                    qemu_get_clock(rt_clock));

    if (alarm_timer->flags & ALARM_FLAG_EXPIRED) {
        alarm_timer->flags &= ~(ALARM_FLAG_EXPIRED);
        qemu_rearm_alarm_timer(alarm_timer);
    }

    /* Check bottom-halves last in case any of the earlier events triggered
       them.  */
    qemu_bh_poll();

}

static int main_loop(void)
{
    int ret, timeout;
#ifdef CONFIG_PROFILER
    int64_t ti;
#endif
    CPUState *env;

    cur_cpu = first_cpu;
    next_cpu = cur_cpu->next_cpu ?: first_cpu;
    for(;;) {
        if (vm_running) {

            for(;;) {
                /* get next cpu */
                env = next_cpu;
#ifdef CONFIG_PROFILER
                ti = profile_getclock();
#endif
                if (use_icount) {
                    int64_t count;
                    int decr;
                    qemu_icount -= (env->icount_decr.u16.low + env->icount_extra);
                    env->icount_decr.u16.low = 0;
                    env->icount_extra = 0;
                    count = qemu_next_deadline();
                    count = (count + (1 << icount_time_shift) - 1)
                            >> icount_time_shift;
                    qemu_icount += count;
                    decr = (count > 0xffff) ? 0xffff : count;
                    count -= decr;
                    env->icount_decr.u16.low = decr;
                    env->icount_extra = count;
                }
                ret = cpu_exec(env);
#ifdef CONFIG_PROFILER
                qemu_time += profile_getclock() - ti;
#endif
                if (use_icount) {
                    /* Fold pending instructions back into the
                       instruction counter, and clear the interrupt flag.  */
                    qemu_icount -= (env->icount_decr.u16.low
                                    + env->icount_extra);
                    env->icount_decr.u32 = 0;
                    env->icount_extra = 0;
                }
                next_cpu = env->next_cpu ?: first_cpu;
                if (event_pending && likely(ret != EXCP_DEBUG)) {
                    ret = EXCP_INTERRUPT;
                    event_pending = 0;
                    break;
                }
                if (ret == EXCP_HLT) {
                    /* Give the next CPU a chance to run.  */
                    cur_cpu = env;
                    continue;
                }
                if (ret != EXCP_HALTED)
                    break;
                /* all CPUs are halted ? */
                if (env == cur_cpu)
                    break;
            }
            cur_cpu = env;

            if (shutdown_requested) {
                ret = EXCP_INTERRUPT;
                if (no_shutdown) {
                    vm_stop(0);
                    no_shutdown = 0;
                }
                else
                    break;
            }
            if (reset_requested) {
                reset_requested = 0;
                qemu_system_reset();
                ret = EXCP_INTERRUPT;
            }
            if (powerdown_requested) {
                powerdown_requested = 0;
		qemu_system_powerdown();
                ret = EXCP_INTERRUPT;
            }
            if (unlikely(ret == EXCP_DEBUG)) {
                vm_stop(EXCP_DEBUG);
            }
            /* If all cpus are halted then wait until the next IRQ */
            /* XXX: use timeout computed from timers */
            if (ret == EXCP_HALTED) {
                if (use_icount) {
                    int64_t add;
                    int64_t delta;
                    /* Advance virtual time to the next event.  */
                    if (use_icount == 1) {
                        /* When not using an adaptive execution frequency
                           we tend to get badly out of sync with real time,
                           so just delay for a reasonable amount of time.  */
                        delta = 0;
                    } else {
                        delta = cpu_get_icount() - cpu_get_clock();
                    }
                    if (delta > 0) {
                        /* If virtual time is ahead of real time then just
                           wait for IO.  */
                        timeout = (delta / 1000000) + 1;
                    } else {
                        /* Wait for either IO to occur or the next
                           timer event.  */
                        add = qemu_next_deadline();
                        /* We advance the timer before checking for IO.
                           Limit the amount we advance so that early IO
                           activity won't get the guest too far ahead.  */
                        if (add > 10000000)
                            add = 10000000;
                        delta += add;
                        add = (add + (1 << icount_time_shift) - 1)
                              >> icount_time_shift;
                        qemu_icount += add;
                        timeout = delta / 1000000;
                        if (timeout < 0)
                            timeout = 0;
                    }
                } else {
                    timeout = 10;
                }
            } else {
                timeout = 0;
            }
        } else {
            if (shutdown_requested) {
                ret = EXCP_INTERRUPT;
                break;
            }
            timeout = 10;
        }
#ifdef CONFIG_PROFILER
        ti = profile_getclock();
#endif
        main_loop_wait(timeout);
#ifdef CONFIG_PROFILER
        dev_time += profile_getclock() - ti;
#endif
    }
    cpu_disable_ticks();
    return ret;
}

static void help(int exitcode)
{
    printf("QEMU PC emulator version " QEMU_VERSION ", Copyright (c) 2003-2008 Fabrice Bellard\n"
           "usage: %s [options] [disk_image]\n"
           "\n"
           "'disk_image' is a raw hard image image for IDE hard disk 0\n"
           "\n"
           "Standard options:\n"
           "-M machine      select emulated machine (-M ? for list)\n"
           "-cpu cpu        select CPU (-cpu ? for list)\n"
           "-fda/-fdb file  use 'file' as floppy disk 0/1 image\n"
           "-hda/-hdb file  use 'file' as IDE hard disk 0/1 image\n"
           "-hdc/-hdd file  use 'file' as IDE hard disk 2/3 image\n"
           "-cdrom file     use 'file' as IDE cdrom image (cdrom is ide1 master)\n"
	   "-drive [file=file][,if=type][,bus=n][,unit=m][,media=d][,index=i]\n"
           "       [,cyls=c,heads=h,secs=s[,trans=t]][,snapshot=on|off]\n"
           "       [,cache=on|off][,format=f]\n"
	   "                use 'file' as a drive image\n"
           "-mtdblock file  use 'file' as on-board Flash memory image\n"
           "-sd file        use 'file' as SecureDigital card image\n"
           "-pflash file    use 'file' as a parallel flash image\n"
           "-boot [a|c|d|n] boot on floppy (a), hard disk (c), CD-ROM (d), or network (n)\n"
           "-snapshot       write to temporary files instead of disk image files\n"
#ifdef CONFIG_SDL
           "-no-frame       open SDL window without a frame and window decorations\n"
           "-alt-grab       use Ctrl-Alt-Shift to grab mouse (instead of Ctrl-Alt)\n"
           "-no-quit        disable SDL window close capability\n"
#endif
#ifdef TARGET_I386
           "-no-fd-bootchk  disable boot signature checking for floppy disks\n"
#endif
           "-m megs         set virtual RAM size to megs MB [default=%d]\n"
           "-smp n          set the number of CPUs to 'n' [default=1]\n"
           "-nographic      disable graphical output and redirect serial I/Os to console\n"
           "-portrait       rotate graphical output 90 deg left (only PXA LCD)\n"
#ifndef _WIN32
           "-k language     use keyboard layout (for example \"fr\" for French)\n"
#endif
#ifdef HAS_AUDIO
           "-audio-help     print list of audio drivers and their options\n"
           "-soundhw c1,... enable audio support\n"
           "                and only specified sound cards (comma separated list)\n"
           "                use -soundhw ? to get the list of supported cards\n"
           "                use -soundhw all to enable all of them\n"
#endif
           "-vga [std|cirrus|vmware]\n"
           "                select video card type\n"
           "-localtime      set the real time clock to local time [default=utc]\n"
           "-full-screen    start in full screen\n"
#ifdef TARGET_I386
           "-win2k-hack     use it when installing Windows 2000 to avoid a disk full bug\n"
#endif
           "-usb            enable the USB driver (will be the default soon)\n"
           "-usbdevice name add the host or guest USB device 'name'\n"
#if defined(TARGET_PPC) || defined(TARGET_SPARC)
           "-g WxH[xDEPTH]  Set the initial graphical resolution and depth\n"
#endif
           "-name string    set the name of the guest\n"
           "-uuid %%08x-%%04x-%%04x-%%04x-%%012x specify machine UUID\n"
           "\n"
           "Network options:\n"
           "-net nic[,vlan=n][,macaddr=addr][,model=type]\n"
           "                create a new Network Interface Card and connect it to VLAN 'n'\n"
#ifdef CONFIG_SLIRP
           "-net user[,vlan=n][,hostname=host]\n"
           "                connect the user mode network stack to VLAN 'n' and send\n"
           "                hostname 'host' to DHCP clients\n"
#endif
#ifdef _WIN32
           "-net tap[,vlan=n],ifname=name\n"
           "                connect the host TAP network interface to VLAN 'n'\n"
#else
           "-net tap[,vlan=n][,fd=h][,ifname=name][,script=file][,downscript=dfile]\n"
           "                connect the host TAP network interface to VLAN 'n' and use the\n"
           "                network scripts 'file' (default=%s)\n"
           "                and 'dfile' (default=%s);\n"
           "                use '[down]script=no' to disable script execution;\n"
           "                use 'fd=h' to connect to an already opened TAP interface\n"
#endif
           "-net socket[,vlan=n][,fd=h][,listen=[host]:port][,connect=host:port]\n"
           "                connect the vlan 'n' to another VLAN using a socket connection\n"
           "-net socket[,vlan=n][,fd=h][,mcast=maddr:port]\n"
           "                connect the vlan 'n' to multicast maddr and port\n"
#ifdef CONFIG_VDE
           "-net vde[,vlan=n][,sock=socketpath][,port=n][,group=groupname][,mode=octalmode]\n"
           "                connect the vlan 'n' to port 'n' of a vde switch running\n"
           "                on host and listening for incoming connections on 'socketpath'.\n"
           "                Use group 'groupname' and mode 'octalmode' to change default\n"
           "                ownership and permissions for communication port.\n"
#endif
           "-net none       use it alone to have zero network devices; if no -net option\n"
           "                is provided, the default is '-net nic -net user'\n"
           "\n"
#ifdef CONFIG_SLIRP
           "-tftp dir       allow tftp access to files in dir [-net user]\n"
           "-bootp file     advertise file in BOOTP replies\n"
#ifndef _WIN32
           "-smb dir        allow SMB access to files in 'dir' [-net user]\n"
#endif
           "-redir [tcp|udp]:host-port:[guest-host]:guest-port\n"
           "                redirect TCP or UDP connections from host to guest [-net user]\n"
#endif
           "\n"
           "Linux boot specific:\n"
           "-kernel bzImage use 'bzImage' as kernel image\n"
           "-append cmdline use 'cmdline' as kernel command line\n"
           "-initrd file    use 'file' as initial ram disk\n"
           "\n"
           "Debug/Expert options:\n"
           "-monitor dev    redirect the monitor to char device 'dev'\n"
           "-serial dev     redirect the serial port to char device 'dev'\n"
           "-parallel dev   redirect the parallel port to char device 'dev'\n"
           "-pidfile file   Write PID to 'file'\n"
           "-S              freeze CPU at startup (use 'c' to start execution)\n"
           "-s              wait gdb connection to port\n"
           "-p port         set gdb connection port [default=%s]\n"
           "-d item1,...    output log to %s (use -d ? for a list of log items)\n"
           "-hdachs c,h,s[,t]  force hard disk 0 physical geometry and the optional BIOS\n"
           "                translation (t=none or lba) (usually qemu can guess them)\n"
           "-L path         set the directory for the BIOS, VGA BIOS and keymaps\n"
#ifdef USE_KQEMU
           "-kernel-kqemu   enable KQEMU full virtualization (default is user mode only)\n"
           "-no-kqemu       disable KQEMU kernel module usage\n"
#endif
#ifdef TARGET_I386
           "-no-acpi        disable ACPI\n"
#endif
#ifdef CONFIG_CURSES
           "-curses         use a curses/ncurses interface instead of SDL\n"
#endif
           "-no-reboot      exit instead of rebooting\n"
           "-no-shutdown    stop before shutdown\n"
           "-loadvm [tag|id]  start right away with a saved state (loadvm in monitor)\n"
	   "-vnc display    start a VNC server on display\n"
#ifndef _WIN32
	   "-daemonize      daemonize QEMU after initializing\n"
#endif
	   "-option-rom rom load a file, rom, into the option ROM space\n"
#ifdef TARGET_SPARC
           "-prom-env variable=value  set OpenBIOS nvram variables\n"
#endif
           "-clock          force the use of the given methods for timer alarm.\n"
           "                To see what timers are available use -clock ?\n"
           "-startdate      select initial date of the clock\n"
           "-icount [N|auto]\n"
           "                Enable virtual instruction counter with 2^N clock ticks per instruction\n"
           "\n"
           "During emulation, the following keys are useful:\n"
           "ctrl-alt-f      toggle full screen\n"
           "ctrl-alt-n      switch to virtual console 'n'\n"
           "ctrl-alt        toggle mouse and keyboard grab\n"
           "\n"
           "When using -nographic, press 'ctrl-a h' to get some help.\n"
           ,
           "qemu",
           DEFAULT_RAM_SIZE,
#ifndef _WIN32
           DEFAULT_NETWORK_SCRIPT,
           DEFAULT_NETWORK_DOWN_SCRIPT,
#endif
           DEFAULT_GDBSTUB_PORT,
           "/tmp/qemu.log");
    exit(exitcode);
}

#define HAS_ARG 0x0001

enum {
    QEMU_OPTION_h,

    QEMU_OPTION_M,
    QEMU_OPTION_cpu,
    QEMU_OPTION_fda,
    QEMU_OPTION_fdb,
    QEMU_OPTION_hda,
    QEMU_OPTION_hdb,
    QEMU_OPTION_hdc,
    QEMU_OPTION_hdd,
    QEMU_OPTION_drive,
    QEMU_OPTION_cdrom,
    QEMU_OPTION_mtdblock,
    QEMU_OPTION_sd,
    QEMU_OPTION_pflash,
    QEMU_OPTION_boot,
    QEMU_OPTION_snapshot,
#ifdef TARGET_I386
    QEMU_OPTION_no_fd_bootchk,
#endif
    QEMU_OPTION_m,
    QEMU_OPTION_nographic,
    QEMU_OPTION_portrait,
#ifdef HAS_AUDIO
    QEMU_OPTION_audio_help,
    QEMU_OPTION_soundhw,
#endif

    QEMU_OPTION_net,
    QEMU_OPTION_tftp,
    QEMU_OPTION_bootp,
    QEMU_OPTION_smb,
    QEMU_OPTION_redir,

    QEMU_OPTION_kernel,
    QEMU_OPTION_append,
    QEMU_OPTION_initrd,

    QEMU_OPTION_S,
    QEMU_OPTION_s,
    QEMU_OPTION_p,
    QEMU_OPTION_d,
    QEMU_OPTION_hdachs,
    QEMU_OPTION_L,
    QEMU_OPTION_bios,
    QEMU_OPTION_k,
    QEMU_OPTION_localtime,
    QEMU_OPTION_g,
    QEMU_OPTION_vga,
    QEMU_OPTION_echr,
    QEMU_OPTION_monitor,
    QEMU_OPTION_serial,
    QEMU_OPTION_parallel,
    QEMU_OPTION_loadvm,
    QEMU_OPTION_full_screen,
    QEMU_OPTION_no_frame,
    QEMU_OPTION_alt_grab,
    QEMU_OPTION_no_quit,
    QEMU_OPTION_pidfile,
    QEMU_OPTION_no_kqemu,
    QEMU_OPTION_kernel_kqemu,
    QEMU_OPTION_win2k_hack,
    QEMU_OPTION_usb,
    QEMU_OPTION_usbdevice,
    QEMU_OPTION_smp,
    QEMU_OPTION_vnc,
    QEMU_OPTION_no_acpi,
    QEMU_OPTION_curses,
    QEMU_OPTION_no_reboot,
    QEMU_OPTION_no_shutdown,
    QEMU_OPTION_show_cursor,
    QEMU_OPTION_daemonize,
    QEMU_OPTION_option_rom,
    QEMU_OPTION_semihosting,
    QEMU_OPTION_name,
    QEMU_OPTION_prom_env,
    QEMU_OPTION_old_param,
    QEMU_OPTION_clock,
    QEMU_OPTION_startdate,
    QEMU_OPTION_tb_size,
    QEMU_OPTION_icount,
    QEMU_OPTION_uuid,
};

typedef struct QEMUOption {
    const char *name;
    int flags;
    int index;
} QEMUOption;

static const QEMUOption qemu_options[] = {
    { "h", 0, QEMU_OPTION_h },
    { "help", 0, QEMU_OPTION_h },

    { "M", HAS_ARG, QEMU_OPTION_M },
    { "cpu", HAS_ARG, QEMU_OPTION_cpu },
    { "fda", HAS_ARG, QEMU_OPTION_fda },
    { "fdb", HAS_ARG, QEMU_OPTION_fdb },
    { "hda", HAS_ARG, QEMU_OPTION_hda },
    { "hdb", HAS_ARG, QEMU_OPTION_hdb },
    { "hdc", HAS_ARG, QEMU_OPTION_hdc },
    { "hdd", HAS_ARG, QEMU_OPTION_hdd },
    { "drive", HAS_ARG, QEMU_OPTION_drive },
    { "cdrom", HAS_ARG, QEMU_OPTION_cdrom },
    { "mtdblock", HAS_ARG, QEMU_OPTION_mtdblock },
    { "sd", HAS_ARG, QEMU_OPTION_sd },
    { "pflash", HAS_ARG, QEMU_OPTION_pflash },
    { "boot", HAS_ARG, QEMU_OPTION_boot },
    { "snapshot", 0, QEMU_OPTION_snapshot },
#ifdef TARGET_I386
    { "no-fd-bootchk", 0, QEMU_OPTION_no_fd_bootchk },
#endif
    { "m", HAS_ARG, QEMU_OPTION_m },
    { "nographic", 0, QEMU_OPTION_nographic },
    { "portrait", 0, QEMU_OPTION_portrait },
    { "k", HAS_ARG, QEMU_OPTION_k },
#ifdef HAS_AUDIO
    { "audio-help", 0, QEMU_OPTION_audio_help },
    { "soundhw", HAS_ARG, QEMU_OPTION_soundhw },
#endif

    { "net", HAS_ARG, QEMU_OPTION_net},
#ifdef CONFIG_SLIRP
    { "tftp", HAS_ARG, QEMU_OPTION_tftp },
    { "bootp", HAS_ARG, QEMU_OPTION_bootp },
#ifndef _WIN32
    { "smb", HAS_ARG, QEMU_OPTION_smb },
#endif
    { "redir", HAS_ARG, QEMU_OPTION_redir },
#endif

    { "kernel", HAS_ARG, QEMU_OPTION_kernel },
    { "append", HAS_ARG, QEMU_OPTION_append },
    { "initrd", HAS_ARG, QEMU_OPTION_initrd },

    { "S", 0, QEMU_OPTION_S },
    { "s", 0, QEMU_OPTION_s },
    { "p", HAS_ARG, QEMU_OPTION_p },
    { "d", HAS_ARG, QEMU_OPTION_d },
    { "hdachs", HAS_ARG, QEMU_OPTION_hdachs },
    { "L", HAS_ARG, QEMU_OPTION_L },
    { "bios", HAS_ARG, QEMU_OPTION_bios },
#ifdef USE_KQEMU
    { "no-kqemu", 0, QEMU_OPTION_no_kqemu },
    { "kernel-kqemu", 0, QEMU_OPTION_kernel_kqemu },
#endif
#if defined(TARGET_PPC) || defined(TARGET_SPARC)
    { "g", 1, QEMU_OPTION_g },
#endif
    { "localtime", 0, QEMU_OPTION_localtime },
    { "vga", HAS_ARG, QEMU_OPTION_vga },
    { "echr", HAS_ARG, QEMU_OPTION_echr },
    { "monitor", HAS_ARG, QEMU_OPTION_monitor },
    { "serial", HAS_ARG, QEMU_OPTION_serial },
    { "parallel", HAS_ARG, QEMU_OPTION_parallel },
    { "loadvm", HAS_ARG, QEMU_OPTION_loadvm },
    { "full-screen", 0, QEMU_OPTION_full_screen },
#ifdef CONFIG_SDL
    { "no-frame", 0, QEMU_OPTION_no_frame },
    { "alt-grab", 0, QEMU_OPTION_alt_grab },
    { "no-quit", 0, QEMU_OPTION_no_quit },
#endif
    { "pidfile", HAS_ARG, QEMU_OPTION_pidfile },
    { "win2k-hack", 0, QEMU_OPTION_win2k_hack },
    { "usbdevice", HAS_ARG, QEMU_OPTION_usbdevice },
    { "smp", HAS_ARG, QEMU_OPTION_smp },
    { "vnc", HAS_ARG, QEMU_OPTION_vnc },
#ifdef CONFIG_CURSES
    { "curses", 0, QEMU_OPTION_curses },
#endif
    { "uuid", HAS_ARG, QEMU_OPTION_uuid },

    /* temporary options */
    { "usb", 0, QEMU_OPTION_usb },
    { "no-acpi", 0, QEMU_OPTION_no_acpi },
    { "no-reboot", 0, QEMU_OPTION_no_reboot },
    { "no-shutdown", 0, QEMU_OPTION_no_shutdown },
    { "show-cursor", 0, QEMU_OPTION_show_cursor },
    { "daemonize", 0, QEMU_OPTION_daemonize },
    { "option-rom", HAS_ARG, QEMU_OPTION_option_rom },
#if defined(TARGET_ARM) || defined(TARGET_M68K)
    { "semihosting", 0, QEMU_OPTION_semihosting },
#endif
    { "name", HAS_ARG, QEMU_OPTION_name },
#if defined(TARGET_SPARC)
    { "prom-env", HAS_ARG, QEMU_OPTION_prom_env },
#endif
#if defined(TARGET_ARM)
    { "old-param", 0, QEMU_OPTION_old_param },
#endif
    { "clock", HAS_ARG, QEMU_OPTION_clock },
    { "startdate", HAS_ARG, QEMU_OPTION_startdate },
    { "tb-size", HAS_ARG, QEMU_OPTION_tb_size },
    { "icount", HAS_ARG, QEMU_OPTION_icount },
    { NULL },
};

/* password input */

int qemu_key_check(BlockDriverState *bs, const char *name)
{
    char password[256];
    int i;

    if (!bdrv_is_encrypted(bs))
        return 0;

    term_printf("%s is encrypted.\n", name);
    for(i = 0; i < 3; i++) {
        monitor_readline("Password: ", 1, password, sizeof(password));
        if (bdrv_set_key(bs, password) == 0)
            return 0;
        term_printf("invalid password\n");
    }
    return -EPERM;
}

static BlockDriverState *get_bdrv(int index)
{
    if (index > nb_drives)
        return NULL;
    return drives_table[index].bdrv;
}

static void read_passwords(void)
{
    BlockDriverState *bs;
    int i;

    for(i = 0; i < 6; i++) {
        bs = get_bdrv(i);
        if (bs)
            qemu_key_check(bs, bdrv_get_device_name(bs));
    }
}

#ifdef HAS_AUDIO
struct soundhw soundhw[] = {
#ifdef HAS_AUDIO_CHOICE
#if defined(TARGET_I386) || defined(TARGET_MIPS)
    {
        "pcspk",
        "PC speaker",
        0,
        1,
        { .init_isa = pcspk_audio_init }
    },
#endif
    {
        "sb16",
        "Creative Sound Blaster 16",
        0,
        1,
        { .init_isa = SB16_init }
    },

#ifdef CONFIG_CS4231A
    {
        "cs4231a",
        "CS4231A",
        0,
        1,
        { .init_isa = cs4231a_init }
    },
#endif

#ifdef CONFIG_ADLIB
    {
        "adlib",
#ifdef HAS_YMF262
        "Yamaha YMF262 (OPL3)",
#else
        "Yamaha YM3812 (OPL2)",
#endif
        0,
        1,
        { .init_isa = Adlib_init }
    },
#endif

#ifdef CONFIG_GUS
    {
        "gus",
        "Gravis Ultrasound GF1",
        0,
        1,
        { .init_isa = GUS_init }
    },
#endif

#ifdef CONFIG_AC97
    {
        "ac97",
        "Intel 82801AA AC97 Audio",
        0,
        0,
        { .init_pci = ac97_init }
    },
#endif

    {
        "es1370",
        "ENSONIQ AudioPCI ES1370",
        0,
        0,
        { .init_pci = es1370_init }
    },
#endif

    { NULL, NULL, 0, 0, { NULL } }
};

static void select_soundhw (const char *optarg)
{
    struct soundhw *c;

    if (*optarg == '?') {
    show_valid_cards:

        printf ("Valid sound card names (comma separated):\n");
        for (c = soundhw; c->name; ++c) {
            printf ("%-11s %s\n", c->name, c->descr);
        }
        printf ("\n-soundhw all will enable all of the above\n");
        exit (*optarg != '?');
    }
    else {
        size_t l;
        const char *p;
        char *e;
        int bad_card = 0;

        if (!strcmp (optarg, "all")) {
            for (c = soundhw; c->name; ++c) {
                c->enabled = 1;
            }
            return;
        }

        p = optarg;
        while (*p) {
            e = strchr (p, ',');
            l = !e ? strlen (p) : (size_t) (e - p);

            for (c = soundhw; c->name; ++c) {
                if (!strncmp (c->name, p, l)) {
                    c->enabled = 1;
                    break;
                }
            }

            if (!c->name) {
                if (l > 80) {
                    fprintf (stderr,
                             "Unknown sound card name (too big to show)\n");
                }
                else {
                    fprintf (stderr, "Unknown sound card name `%.*s'\n",
                             (int) l, p);
                }
                bad_card = 1;
            }
            p += l + (e != NULL);
        }

        if (bad_card)
            goto show_valid_cards;
    }
}
#endif

static void select_vgahw (const char *p)
{
    const char *opts;

    if (strstart(p, "std", &opts)) {
        cirrus_vga_enabled = 0;
        vmsvga_enabled = 0;
    } else if (strstart(p, "cirrus", &opts)) {
        cirrus_vga_enabled = 1;
        vmsvga_enabled = 0;
    } else if (strstart(p, "vmware", &opts)) {
        cirrus_vga_enabled = 0;
        vmsvga_enabled = 1;
    } else {
    invalid_vga:
        fprintf(stderr, "Unknown vga type: %s\n", p);
        exit(1);
    }
    while (*opts) {
        const char *nextopt;

        if (strstart(opts, ",retrace=", &nextopt)) {
            opts = nextopt;
            if (strstart(opts, "dumb", &nextopt))
                vga_retrace_method = VGA_RETRACE_DUMB;
            else if (strstart(opts, "precise", &nextopt))
                vga_retrace_method = VGA_RETRACE_PRECISE;
            else goto invalid_vga;
        } else goto invalid_vga;
        opts = nextopt;
    }
}

#ifdef _WIN32
static BOOL WINAPI qemu_ctrl_handler(DWORD type)
{
    exit(STATUS_CONTROL_C_EXIT);
    return TRUE;
}
#endif

static int qemu_uuid_parse(const char *str, uint8_t *uuid)
{
    int ret;

    if(strlen(str) != 36)
        return -1;

    ret = sscanf(str, UUID_FMT, &uuid[0], &uuid[1], &uuid[2], &uuid[3],
            &uuid[4], &uuid[5], &uuid[6], &uuid[7], &uuid[8], &uuid[9],
            &uuid[10], &uuid[11], &uuid[12], &uuid[13], &uuid[14], &uuid[15]);

    if(ret != 16)
        return -1;

    return 0;
}

#define MAX_NET_CLIENTS 32

#ifndef _WIN32

static void termsig_handler(int signal)
{
    qemu_system_shutdown_request();
}

static void termsig_setup(void)
{
    struct sigaction act;

    memset(&act, 0, sizeof(act));
    act.sa_handler = termsig_handler;
    sigaction(SIGINT,  &act, NULL);
    sigaction(SIGHUP,  &act, NULL);
    sigaction(SIGTERM, &act, NULL);
}

#endif

int main(int argc, char **argv)
{
#ifdef CONFIG_GDBSTUB
    int use_gdbstub;
    const char *gdbstub_port;
#endif
    uint32_t boot_devices_bitmap = 0;
    int i;
    int snapshot, linux_boot, net_boot;
    const char *initrd_filename;
    const char *kernel_filename, *kernel_cmdline;
    const char *boot_devices = "";
    DisplayState *ds = &display_state;
    int cyls, heads, secs, translation;
    const char *net_clients[MAX_NET_CLIENTS];
    int nb_net_clients;
    int hda_index;
    int optind;
    const char *r, *optarg;
    CharDriverState *monitor_hd;
    const char *monitor_device;
    const char *serial_devices[MAX_SERIAL_PORTS];
    int serial_device_index;
    const char *parallel_devices[MAX_PARALLEL_PORTS];
    int parallel_device_index;
    const char *loadvm = NULL;
    QEMUMachine *machine;
    const char *cpu_model;
    const char *usb_devices[MAX_USB_CMDLINE];
    int usb_devices_index;
    int fds[2];
    int tb_size;
    const char *pid_file = NULL;
    VLANState *vlan;
    int autostart;

    LIST_INIT (&vm_change_state_head);
#ifndef _WIN32
    {
        struct sigaction act;
        sigfillset(&act.sa_mask);
        act.sa_flags = 0;
        act.sa_handler = SIG_IGN;
        sigaction(SIGPIPE, &act, NULL);
    }
#else
    SetConsoleCtrlHandler(qemu_ctrl_handler, TRUE);
    /* Note: cpu_interrupt() is currently not SMP safe, so we force
       QEMU to run on a single CPU */
    {
        HANDLE h;
        DWORD mask, smask;
        int i;
        h = GetCurrentProcess();
        if (GetProcessAffinityMask(h, &mask, &smask)) {
            for(i = 0; i < 32; i++) {
                if (mask & (1 << i))
                    break;
            }
            if (i != 32) {
                mask = 1 << i;
                SetProcessAffinityMask(h, mask);
            }
        }
    }
#endif

    register_machines();
    machine = first_machine;
    cpu_model = NULL;
    initrd_filename = NULL;
    ram_size = 0;
    vga_ram_size = VGA_RAM_SIZE;
#ifdef CONFIG_GDBSTUB
    use_gdbstub = 0;
    gdbstub_port = DEFAULT_GDBSTUB_PORT;
#endif
    snapshot = 0;
    nographic = 0;
    curses = 0;
    kernel_filename = NULL;
    kernel_cmdline = "";
    cyls = heads = secs = 0;
    translation = BIOS_ATA_TRANSLATION_AUTO;
    monitor_device = "vc";

    serial_devices[0] = "vc:80Cx24C";
    for(i = 1; i < MAX_SERIAL_PORTS; i++)
        serial_devices[i] = NULL;
    serial_device_index = 0;

    parallel_devices[0] = "vc:640x480";
    for(i = 1; i < MAX_PARALLEL_PORTS; i++)
        parallel_devices[i] = NULL;
    parallel_device_index = 0;

    usb_devices_index = 0;

    nb_net_clients = 0;
    nb_drives = 0;
    nb_drives_opt = 0;
    hda_index = -1;

    nb_nics = 0;

    tb_size = 0;
    autostart= 1;

    optind = 1;
    for(;;) {
        if (optind >= argc)
            break;
        r = argv[optind];
        if (r[0] != '-') {
	    hda_index = drive_add(argv[optind++], HD_ALIAS, 0);
        } else {
            const QEMUOption *popt;

            optind++;
            /* Treat --foo the same as -foo.  */
            if (r[1] == '-')
                r++;
            popt = qemu_options;
            for(;;) {
                if (!popt->name) {
                    fprintf(stderr, "%s: invalid option -- '%s'\n",
                            argv[0], r);
                    exit(1);
                }
                if (!strcmp(popt->name, r + 1))
                    break;
                popt++;
            }
            if (popt->flags & HAS_ARG) {
                if (optind >= argc) {
                    fprintf(stderr, "%s: option '%s' requires an argument\n",
                            argv[0], r);
                    exit(1);
                }
                optarg = argv[optind++];
            } else {
                optarg = NULL;
            }

            switch(popt->index) {
            case QEMU_OPTION_M:
                machine = find_machine(optarg);
                if (!machine) {
                    QEMUMachine *m;
                    printf("Supported machines are:\n");
                    for(m = first_machine; m != NULL; m = m->next) {
                        printf("%-10s %s%s\n",
                               m->name, m->desc,
                               m == first_machine ? " (default)" : "");
                    }
                    exit(*optarg != '?');
                }
                break;
            case QEMU_OPTION_cpu:
                /* hw initialization will check this */
                if (*optarg == '?') {
/* XXX: implement xxx_cpu_list for targets that still miss it */
#if defined(cpu_list)
                    cpu_list(stdout, &fprintf);
#endif
                    exit(0);
                } else {
                    cpu_model = optarg;
                }
                break;
            case QEMU_OPTION_initrd:
                initrd_filename = optarg;
                break;
            case QEMU_OPTION_hda:
                if (cyls == 0)
                    hda_index = drive_add(optarg, HD_ALIAS, 0);
                else
                    hda_index = drive_add(optarg, HD_ALIAS
			     ",cyls=%d,heads=%d,secs=%d%s",
                             0, cyls, heads, secs,
                             translation == BIOS_ATA_TRANSLATION_LBA ?
                                 ",trans=lba" :
                             translation == BIOS_ATA_TRANSLATION_NONE ?
                                 ",trans=none" : "");
                 break;
            case QEMU_OPTION_hdb:
            case QEMU_OPTION_hdc:
            case QEMU_OPTION_hdd:
                drive_add(optarg, HD_ALIAS, popt->index - QEMU_OPTION_hda);
                break;
            case QEMU_OPTION_drive:
                drive_add(NULL, "%s", optarg);
	        break;
            case QEMU_OPTION_mtdblock:
                drive_add(optarg, MTD_ALIAS);
                break;
            case QEMU_OPTION_sd:
                drive_add(optarg, SD_ALIAS);
                break;
            case QEMU_OPTION_pflash:
                drive_add(optarg, PFLASH_ALIAS);
                break;
            case QEMU_OPTION_snapshot:
                snapshot = 1;
                break;
            case QEMU_OPTION_hdachs:
                {
                    const char *p;
                    p = optarg;
                    cyls = strtol(p, (char **)&p, 0);
                    if (cyls < 1 || cyls > 16383)
                        goto chs_fail;
                    if (*p != ',')
                        goto chs_fail;
                    p++;
                    heads = strtol(p, (char **)&p, 0);
                    if (heads < 1 || heads > 16)
                        goto chs_fail;
                    if (*p != ',')
                        goto chs_fail;
                    p++;
                    secs = strtol(p, (char **)&p, 0);
                    if (secs < 1 || secs > 63)
                        goto chs_fail;
                    if (*p == ',') {
                        p++;
                        if (!strcmp(p, "none"))
                            translation = BIOS_ATA_TRANSLATION_NONE;
                        else if (!strcmp(p, "lba"))
                            translation = BIOS_ATA_TRANSLATION_LBA;
                        else if (!strcmp(p, "auto"))
                            translation = BIOS_ATA_TRANSLATION_AUTO;
                        else
                            goto chs_fail;
                    } else if (*p != '\0') {
                    chs_fail:
                        fprintf(stderr, "qemu: invalid physical CHS format\n");
                        exit(1);
                    }
		    if (hda_index != -1)
                        snprintf(drives_opt[hda_index].opt,
                                 sizeof(drives_opt[hda_index].opt),
                                 HD_ALIAS ",cyls=%d,heads=%d,secs=%d%s",
                                 0, cyls, heads, secs,
			         translation == BIOS_ATA_TRANSLATION_LBA ?
			     	    ",trans=lba" :
			         translation == BIOS_ATA_TRANSLATION_NONE ?
			             ",trans=none" : "");
                }
                break;
            case QEMU_OPTION_nographic:
                nographic = 1;
                break;
#ifdef CONFIG_CURSES
            case QEMU_OPTION_curses:
                curses = 1;
                break;
#endif
            case QEMU_OPTION_portrait:
                graphic_rotate = 1;
                break;
            case QEMU_OPTION_kernel:
                kernel_filename = optarg;
                break;
            case QEMU_OPTION_append:
                kernel_cmdline = optarg;
                break;
            case QEMU_OPTION_cdrom:
                drive_add(optarg, CDROM_ALIAS);
                break;
            case QEMU_OPTION_boot:
                boot_devices = optarg;
                /* We just do some generic consistency checks */
                {
                    /* Could easily be extended to 64 devices if needed */
                    const char *p;
                    
                    boot_devices_bitmap = 0;
                    for (p = boot_devices; *p != '\0'; p++) {
                        /* Allowed boot devices are:
                         * a b     : floppy disk drives
                         * c ... f : IDE disk drives
                         * g ... m : machine implementation dependant drives
                         * n ... p : network devices
                         * It's up to each machine implementation to check
                         * if the given boot devices match the actual hardware
                         * implementation and firmware features.
                         */
                        if (*p < 'a' || *p > 'q') {
                            fprintf(stderr, "Invalid boot device '%c'\n", *p);
                            exit(1);
                        }
                        if (boot_devices_bitmap & (1 << (*p - 'a'))) {
                            fprintf(stderr,
                                    "Boot device '%c' was given twice\n",*p);
                            exit(1);
                        }
                        boot_devices_bitmap |= 1 << (*p - 'a');
                    }
                }
                break;
            case QEMU_OPTION_fda:
            case QEMU_OPTION_fdb:
                drive_add(optarg, FD_ALIAS, popt->index - QEMU_OPTION_fda);
                break;
#ifdef TARGET_I386
            case QEMU_OPTION_no_fd_bootchk:
                fd_bootchk = 0;
                break;
#endif
            case QEMU_OPTION_net:
                if (nb_net_clients >= MAX_NET_CLIENTS) {
                    fprintf(stderr, "qemu: too many network clients\n");
                    exit(1);
                }
                net_clients[nb_net_clients] = optarg;
                nb_net_clients++;
                break;
#ifdef CONFIG_SLIRP
            case QEMU_OPTION_tftp:
		tftp_prefix = optarg;
                break;
            case QEMU_OPTION_bootp:
                bootp_filename = optarg;
                break;
#ifndef _WIN32
            case QEMU_OPTION_smb:
		net_slirp_smb(optarg);
                break;
#endif
            case QEMU_OPTION_redir:
                net_slirp_redir(optarg);
                break;
#endif
#ifdef HAS_AUDIO
            case QEMU_OPTION_audio_help:
                AUD_help ();
                exit (0);
                break;
            case QEMU_OPTION_soundhw:
                select_soundhw (optarg);
                break;
#endif
            case QEMU_OPTION_h:
                help(0);
                break;
            case QEMU_OPTION_m: {
                uint64_t value;
                char *ptr;

                value = strtoul(optarg, &ptr, 10);
                switch (*ptr) {
                case 0: case 'M': case 'm':
                    value <<= 20;
                    break;
                case 'G': case 'g':
                    value <<= 30;
                    break;
                default:
                    fprintf(stderr, "qemu: invalid ram size: %s\n", optarg);
                    exit(1);
                }

                /* On 32-bit hosts, QEMU is limited by virtual address space */
                if (value > (2047 << 20)
#ifndef USE_KQEMU
                    && HOST_LONG_BITS == 32
#endif
                    ) {
                    fprintf(stderr, "qemu: at most 2047 MB RAM can be simulated\n");
                    exit(1);
                }
                if (value != (uint64_t)(ram_addr_t)value) {
                    fprintf(stderr, "qemu: ram size too large\n");
                    exit(1);
                }
                ram_size = value;
                break;
            }
            case QEMU_OPTION_d:
                {
                    int mask;
                    const CPULogItem *item;

                    mask = cpu_str_to_log_mask(optarg);
                    if (!mask) {
                        printf("Log items (comma separated):\n");
                    for(item = cpu_log_items; item->mask != 0; item++) {
                        printf("%-10s %s\n", item->name, item->help);
                    }
                    exit(1);
                    }
                    cpu_set_log(mask);
                }
                break;
#ifdef CONFIG_GDBSTUB
            case QEMU_OPTION_s:
                use_gdbstub = 1;
                break;
            case QEMU_OPTION_p:
                gdbstub_port = optarg;
                break;
#endif
            case QEMU_OPTION_L:
                bios_dir = optarg;
                break;
            case QEMU_OPTION_bios:
                bios_name = optarg;
                break;
            case QEMU_OPTION_S:
                autostart = 0;
                break;
	    case QEMU_OPTION_k:
		keyboard_layout = optarg;
		break;
            case QEMU_OPTION_localtime:
                rtc_utc = 0;
                break;
            case QEMU_OPTION_vga:
                select_vgahw (optarg);
                break;
            case QEMU_OPTION_g:
                {
                    const char *p;
                    int w, h, depth;
                    p = optarg;
                    w = strtol(p, (char **)&p, 10);
                    if (w <= 0) {
                    graphic_error:
                        fprintf(stderr, "qemu: invalid resolution or depth\n");
                        exit(1);
                    }
                    if (*p != 'x')
                        goto graphic_error;
                    p++;
                    h = strtol(p, (char **)&p, 10);
                    if (h <= 0)
                        goto graphic_error;
                    if (*p == 'x') {
                        p++;
                        depth = strtol(p, (char **)&p, 10);
                        if (depth != 8 && depth != 15 && depth != 16 &&
                            depth != 24 && depth != 32)
                            goto graphic_error;
                    } else if (*p == '\0') {
                        depth = graphic_depth;
                    } else {
                        goto graphic_error;
                    }

                    graphic_width = w;
                    graphic_height = h;
                    graphic_depth = depth;
                }
                break;
            case QEMU_OPTION_echr:
                {
                    char *r;
                    term_escape_char = strtol(optarg, &r, 0);
                    if (r == optarg)
                        printf("Bad argument to echr\n");
                    break;
                }
            case QEMU_OPTION_monitor:
                monitor_device = optarg;
                break;
            case QEMU_OPTION_serial:
                if (serial_device_index >= MAX_SERIAL_PORTS) {
                    fprintf(stderr, "qemu: too many serial ports\n");
                    exit(1);
                }
                serial_devices[serial_device_index] = optarg;
                serial_device_index++;
                break;
            case QEMU_OPTION_parallel:
                if (parallel_device_index >= MAX_PARALLEL_PORTS) {
                    fprintf(stderr, "qemu: too many parallel ports\n");
                    exit(1);
                }
                parallel_devices[parallel_device_index] = optarg;
                parallel_device_index++;
                break;
	    case QEMU_OPTION_loadvm:
		loadvm = optarg;
		break;
            case QEMU_OPTION_full_screen:
                full_screen = 1;
                break;
#ifdef CONFIG_SDL
            case QEMU_OPTION_no_frame:
                no_frame = 1;
                break;
            case QEMU_OPTION_alt_grab:
                alt_grab = 1;
                break;
            case QEMU_OPTION_no_quit:
                no_quit = 1;
                break;
#endif
            case QEMU_OPTION_pidfile:
                pid_file = optarg;
                break;
#ifdef TARGET_I386
            case QEMU_OPTION_win2k_hack:
                win2k_install_hack = 1;
                break;
#endif
#ifdef USE_KQEMU
            case QEMU_OPTION_no_kqemu:
                kqemu_allowed = 0;
                break;
            case QEMU_OPTION_kernel_kqemu:
                kqemu_allowed = 2;
                break;
#endif
            case QEMU_OPTION_usb:
                usb_enabled = 1;
                break;
            case QEMU_OPTION_usbdevice:
                usb_enabled = 1;
                if (usb_devices_index >= MAX_USB_CMDLINE) {
                    fprintf(stderr, "Too many USB devices\n");
                    exit(1);
                }
                usb_devices[usb_devices_index] = optarg;
                usb_devices_index++;
                break;
            case QEMU_OPTION_smp:
                smp_cpus = atoi(optarg);
                if (smp_cpus < 1) {
                    fprintf(stderr, "Invalid number of CPUs\n");
                    exit(1);
                }
                break;
	    case QEMU_OPTION_vnc:
		vnc_display = optarg;
		break;
            case QEMU_OPTION_no_acpi:
                acpi_enabled = 0;
                break;
            case QEMU_OPTION_no_reboot:
                no_reboot = 1;
                break;
            case QEMU_OPTION_no_shutdown:
                no_shutdown = 1;
                break;
            case QEMU_OPTION_show_cursor:
                cursor_hide = 0;
                break;
            case QEMU_OPTION_uuid:
                if(qemu_uuid_parse(optarg, qemu_uuid) < 0) {
                    fprintf(stderr, "Fail to parse UUID string."
                            " Wrong format.\n");
                    exit(1);
                }
                break;
	    case QEMU_OPTION_daemonize:
		daemonize = 1;
		break;
	    case QEMU_OPTION_option_rom:
		if (nb_option_roms >= MAX_OPTION_ROMS) {
		    fprintf(stderr, "Too many option ROMs\n");
		    exit(1);
		}
		option_rom[nb_option_roms] = optarg;
		nb_option_roms++;
		break;
            case QEMU_OPTION_semihosting:
                semihosting_enabled = 1;
                break;
            case QEMU_OPTION_name:
                qemu_name = optarg;
                break;
#ifdef TARGET_SPARC
            case QEMU_OPTION_prom_env:
                if (nb_prom_envs >= MAX_PROM_ENVS) {
                    fprintf(stderr, "Too many prom variables\n");
                    exit(1);
                }
                prom_envs[nb_prom_envs] = optarg;
                nb_prom_envs++;
                break;
#endif
#ifdef TARGET_ARM
            case QEMU_OPTION_old_param:
                old_param = 1;
                break;
#endif
            case QEMU_OPTION_clock:
                configure_alarms(optarg);
                break;
            case QEMU_OPTION_startdate:
                {
                    struct tm tm;
                    time_t rtc_start_date;
                    if (!strcmp(optarg, "now")) {
                        rtc_date_offset = -1;
                    } else {
                        if (sscanf(optarg, "%d-%d-%dT%d:%d:%d",
                               &tm.tm_year,
                               &tm.tm_mon,
                               &tm.tm_mday,
                               &tm.tm_hour,
                               &tm.tm_min,
                               &tm.tm_sec) == 6) {
                            /* OK */
                        } else if (sscanf(optarg, "%d-%d-%d",
                                          &tm.tm_year,
                                          &tm.tm_mon,
                                          &tm.tm_mday) == 3) {
                            tm.tm_hour = 0;
                            tm.tm_min = 0;
                            tm.tm_sec = 0;
                        } else {
                            goto date_fail;
                        }
                        tm.tm_year -= 1900;
                        tm.tm_mon--;
                        rtc_start_date = mktimegm(&tm);
                        if (rtc_start_date == -1) {
                        date_fail:
                            fprintf(stderr, "Invalid date format. Valid format are:\n"
                                    "'now' or '2006-06-17T16:01:21' or '2006-06-17'\n");
                            exit(1);
                        }
                        rtc_date_offset = time(NULL) - rtc_start_date;
                    }
                }
                break;
            case QEMU_OPTION_tb_size:
                tb_size = strtol(optarg, NULL, 0);
                if (tb_size < 0)
                    tb_size = 0;
                break;
            case QEMU_OPTION_icount:
                use_icount = 1;
                if (strcmp(optarg, "auto") == 0) {
                    icount_time_shift = -1;
                } else {
                    icount_time_shift = strtol(optarg, NULL, 0);
                }
                break;
            }
        }
    }

    if (smp_cpus > machine->max_cpus) {
        fprintf(stderr, "Number of SMP cpus requested (%d), exceeds max cpus "
                "supported by machine `%s' (%d)\n", smp_cpus,  machine->name,
                machine->max_cpus);
        exit(1);
    }

    if (nographic) {
       if (serial_device_index == 0)
           serial_devices[0] = "stdio";
       if (parallel_device_index == 0)
           parallel_devices[0] = "null";
       if (strncmp(monitor_device, "vc", 2) == 0)
           monitor_device = "stdio";
    }

#ifndef _WIN32
    if (daemonize) {
	pid_t pid;

	if (pipe(fds) == -1)
	    exit(1);

	pid = fork();
	if (pid > 0) {
	    uint8_t status;
	    ssize_t len;

	    close(fds[1]);

	again:
            len = read(fds[0], &status, 1);
            if (len == -1 && (errno == EINTR))
                goto again;

            if (len != 1)
                exit(1);
            else if (status == 1) {
                fprintf(stderr, "Could not acquire pidfile\n");
                exit(1);
            } else
                exit(0);
	} else if (pid < 0)
            exit(1);

	setsid();

	pid = fork();
	if (pid > 0)
	    exit(0);
	else if (pid < 0)
	    exit(1);

	umask(027);

        signal(SIGTSTP, SIG_IGN);
        signal(SIGTTOU, SIG_IGN);
        signal(SIGTTIN, SIG_IGN);
    }
#endif

    if (pid_file && qemu_create_pidfile(pid_file) != 0) {
        if (daemonize) {
            uint8_t status = 1;
            write(fds[1], &status, 1);
        } else
            fprintf(stderr, "Could not acquire pid file\n");
        exit(1);
    }

#ifdef USE_KQEMU
    if (smp_cpus > 1)
        kqemu_allowed = 0;
#endif
    linux_boot = (kernel_filename != NULL);
    net_boot = (boot_devices_bitmap >> ('n' - 'a')) & 0xF;

    if (!linux_boot && net_boot == 0 &&
        !machine->nodisk_ok && nb_drives_opt == 0)
        help(1);

    if (!linux_boot && *kernel_cmdline != '\0') {
        fprintf(stderr, "-append only allowed with -kernel option\n");
        exit(1);
    }

    if (!linux_boot && initrd_filename != NULL) {
        fprintf(stderr, "-initrd only allowed with -kernel option\n");
        exit(1);
    }

    /* boot to floppy or the default cd if no hard disk defined yet */
    if (!boot_devices[0]) {
        boot_devices = "cad";
    }
    setvbuf(stdout, NULL, _IOLBF, 0);

    init_timers();
    init_timer_alarm();
    if (use_icount && icount_time_shift < 0) {
        use_icount = 2;
        /* 125MIPS seems a reasonable initial guess at the guest speed.
           It will be corrected fairly quickly anyway.  */
        icount_time_shift = 3;
        init_icount_adjust();
    }

#ifdef _WIN32
    socket_init();
#endif

    /* init network clients */
    if (nb_net_clients == 0) {
        /* if no clients, we use a default config */
        net_clients[nb_net_clients++] = "nic";
#ifdef CONFIG_SLIRP
        net_clients[nb_net_clients++] = "user";
#endif
    }

    for(i = 0;i < nb_net_clients; i++) {
        if (net_client_parse(net_clients[i]) < 0)
            exit(1);
    }
    for(vlan = first_vlan; vlan != NULL; vlan = vlan->next) {
        if (vlan->nb_guest_devs == 0 && vlan->nb_host_devs == 0)
            continue;
        if (vlan->nb_guest_devs == 0)
            fprintf(stderr, "Warning: vlan %d with no nics\n", vlan->id);
        if (vlan->nb_host_devs == 0)
            fprintf(stderr,
                    "Warning: vlan %d is not connected to host network\n",
                    vlan->id);
    }

#ifdef TARGET_I386
    /* XXX: this should be moved in the PC machine instantiation code */
    if (net_boot != 0) {
        int netroms = 0;
	for (i = 0; i < nb_nics && i < 4; i++) {
	    const char *model = nd_table[i].model;
	    char buf[1024];
            if (net_boot & (1 << i)) {
                if (model == NULL)
                    model = "ne2k_pci";
                snprintf(buf, sizeof(buf), "%s/pxe-%s.bin", bios_dir, model);
                if (get_image_size(buf) > 0) {
                    if (nb_option_roms >= MAX_OPTION_ROMS) {
                        fprintf(stderr, "Too many option ROMs\n");
                        exit(1);
                    }
                    option_rom[nb_option_roms] = strdup(buf);
                    nb_option_roms++;
                    netroms++;
                }
            }
	}
	if (netroms == 0) {
	    fprintf(stderr, "No valid PXE rom found for network device\n");
	    exit(1);
	}
    }
#endif

    /* init the memory */
    phys_ram_size = machine->ram_require & ~RAMSIZE_FIXED;

    if (machine->ram_require & RAMSIZE_FIXED) {
        if (ram_size > 0) {
            if (ram_size < phys_ram_size) {
                fprintf(stderr, "Machine `%s' requires %llu bytes of memory\n",
                                machine->name, (unsigned long long) phys_ram_size);
                exit(-1);
            }

            phys_ram_size = ram_size;
        } else
            ram_size = phys_ram_size;
    } else {
        if (ram_size == 0)
            ram_size = DEFAULT_RAM_SIZE * 1024 * 1024;

        phys_ram_size += ram_size;
    }

    phys_ram_base = qemu_vmalloc(phys_ram_size);
    if (!phys_ram_base) {
        fprintf(stderr, "Could not allocate physical memory\n");
        exit(1);
    }

    /* init the dynamic translator */
    cpu_exec_init_all(tb_size * 1024 * 1024);

    bdrv_init();

    /* we always create the cdrom drive, even if no disk is there */

    if (nb_drives_opt < MAX_DRIVES)
        drive_add(NULL, CDROM_ALIAS);

    /* we always create at least one floppy */

    if (nb_drives_opt < MAX_DRIVES)
        drive_add(NULL, FD_ALIAS, 0);

    /* we always create one sd slot, even if no card is in it */

    if (nb_drives_opt < MAX_DRIVES)
        drive_add(NULL, SD_ALIAS);

    /* open the virtual block devices */

    for(i = 0; i < nb_drives_opt; i++)
        if (drive_init(&drives_opt[i], snapshot, machine) == -1)
	    exit(1);

    register_savevm("timer", 0, 2, timer_save, timer_load, NULL);
    register_savevm_live("ram", 0, 3, ram_save_live, NULL, ram_load, NULL);

    /* terminal init */
    memset(&display_state, 0, sizeof(display_state));
    if (nographic) {
        if (curses) {
            fprintf(stderr, "fatal: -nographic can't be used with -curses\n");
            exit(1);
        }
        /* nearly nothing to do */
        dumb_display_init(ds);
    } else if (vnc_display != NULL) {
        vnc_display_init(ds);
        if (vnc_display_open(ds, vnc_display) < 0)
            exit(1);
    } else
#if defined(CONFIG_CURSES)
    if (curses) {
        curses_display_init(ds, full_screen);
    } else
#endif
    {
#if defined(CONFIG_SDL)
        sdl_display_init(ds, full_screen, no_frame);
#elif defined(CONFIG_COCOA)
        cocoa_display_init(ds, full_screen);
#else
        dumb_display_init(ds);
#endif
    }

#ifndef _WIN32
    /* must be after terminal init, SDL library changes signal handlers */
    termsig_setup();
#endif

    /* Maintain compatibility with multiple stdio monitors */
    if (!strcmp(monitor_device,"stdio")) {
        for (i = 0; i < MAX_SERIAL_PORTS; i++) {
            const char *devname = serial_devices[i];
            if (devname && !strcmp(devname,"mon:stdio")) {
                monitor_device = NULL;
                break;
            } else if (devname && !strcmp(devname,"stdio")) {
                monitor_device = NULL;
                serial_devices[i] = "mon:stdio";
                break;
            }
        }
    }
    if (monitor_device) {
        monitor_hd = qemu_chr_open(monitor_device);
        if (!monitor_hd) {
            fprintf(stderr, "qemu: could not open monitor device '%s'\n", monitor_device);
            exit(1);
        }
        monitor_init(monitor_hd, !nographic);
    }

    for(i = 0; i < MAX_SERIAL_PORTS; i++) {
        const char *devname = serial_devices[i];
        if (devname && strcmp(devname, "none")) {
            serial_hds[i] = qemu_chr_open(devname);
            if (!serial_hds[i]) {
                fprintf(stderr, "qemu: could not open serial device '%s'\n",
                        devname);
                exit(1);
            }
            if (strstart(devname, "vc", 0))
                qemu_chr_printf(serial_hds[i], "serial%d console\r\n", i);
        }
    }

    for(i = 0; i < MAX_PARALLEL_PORTS; i++) {
        const char *devname = parallel_devices[i];
        if (devname && strcmp(devname, "none")) {
            parallel_hds[i] = qemu_chr_open(devname);
            if (!parallel_hds[i]) {
                fprintf(stderr, "qemu: could not open parallel device '%s'\n",
                        devname);
                exit(1);
            }
            if (strstart(devname, "vc", 0))
                qemu_chr_printf(parallel_hds[i], "parallel%d console\r\n", i);
        }
    }

    machine->init(ram_size, vga_ram_size, boot_devices, ds,
                  kernel_filename, kernel_cmdline, initrd_filename, cpu_model);

    /* init USB devices */
    if (usb_enabled) {
        for(i = 0; i < usb_devices_index; i++) {
            if (usb_device_add(usb_devices[i]) < 0) {
                fprintf(stderr, "Warning: could not add USB device %s\n",
                        usb_devices[i]);
            }
        }
    }

    if (display_state.dpy_refresh) {
        display_state.gui_timer = qemu_new_timer(rt_clock, gui_update, &display_state);
        qemu_mod_timer(display_state.gui_timer, qemu_get_clock(rt_clock));
    }

#ifdef CONFIG_GDBSTUB
    if (use_gdbstub) {
        /* XXX: use standard host:port notation and modify options
           accordingly. */
        if (gdbserver_start(gdbstub_port) < 0) {
            fprintf(stderr, "qemu: could not open gdbstub device on port '%s'\n",
                    gdbstub_port);
            exit(1);
        }
    }
#endif

    if (loadvm)
        do_loadvm(loadvm);

    {
        /* XXX: simplify init */
        read_passwords();
        if (autostart) {
            vm_start();
        }
    }

    if (daemonize) {
	uint8_t status = 0;
	ssize_t len;
	int fd;

    again1:
	len = write(fds[1], &status, 1);
	if (len == -1 && (errno == EINTR))
	    goto again1;

	if (len != 1)
	    exit(1);

	chdir("/");
	TFR(fd = open("/dev/null", O_RDWR));
	if (fd == -1)
	    exit(1);

	dup2(fd, 0);
	dup2(fd, 1);
	dup2(fd, 2);

	close(fd);
    }

    main_loop();
    quit_timers();

#if !defined(_WIN32)
    /* close network clients */
    for(vlan = first_vlan; vlan != NULL; vlan = vlan->next) {
        VLANClientState *vc;

        for(vc = vlan->first_client; vc != NULL; vc = vc->next) {
            if (vc->fd_read == tap_receive) {
                char ifname[64];
                TAPState *s = vc->opaque;

                if (sscanf(vc->info_str, "tap: ifname=%63s ", ifname) == 1 &&
                    s->down_script[0])
                    launch_script(s->down_script, ifname, s->fd);
            }
#if defined(CONFIG_VDE)
            if (vc->fd_read == vde_from_qemu) {
                VDEState *s = vc->opaque;
                vde_close(s->vde);
            }
#endif
        }
    }
#endif
    return 0;
}
