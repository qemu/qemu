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
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <sys/time.h>
#include <zlib.h>

/* Needed early for HOST_BSD etc. */
#include "config-host.h"

#ifndef _WIN32
#include <pwd.h>
#include <sys/times.h>
#include <sys/wait.h>
#include <termios.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if.h>
#if defined(__NetBSD__)
#include <net/if_tap.h>
#endif
#ifdef __linux__
#include <linux/if_tun.h>
#endif
#include <arpa/inet.h>
#include <dirent.h>
#include <netdb.h>
#include <sys/select.h>
#ifdef HOST_BSD
#include <sys/stat.h>
#if defined(__FreeBSD__) || defined(__DragonFly__)
#include <libutil.h>
#else
#include <util.h>
#endif
#elif defined (__GLIBC__) && defined (__FreeBSD_kernel__)
#include <freebsd/stdlib.h>
#else
#ifdef __linux__
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

#if defined(__OpenBSD__)
#include <util.h>
#endif

#if defined(CONFIG_VDE)
#include <libvdeplug.h>
#endif

#ifdef _WIN32
#include <windows.h>
#include <malloc.h>
#include <sys/timeb.h>
#include <mmsystem.h>
#define getopt_long_only getopt_long
#define memalign(align, size) malloc(size)
#endif

#ifdef CONFIG_SDL
#ifdef __APPLE__
#include <SDL/SDL.h>
int qemu_main(int argc, char **argv, char **envp);
int main(int argc, char **argv)
{
    qemu_main(argc, argv, NULL);
}
#undef main
#define main qemu_main
#endif
#endif /* CONFIG_SDL */

#ifdef CONFIG_COCOA
#undef main
#define main qemu_main
#endif /* CONFIG_COCOA */

#include "hw/hw.h"
#include "hw/boards.h"
#include "hw/usb.h"
#include "hw/pcmcia.h"
#include "hw/pc.h"
#include "hw/audiodev.h"
#include "hw/isa.h"
#include "hw/baum.h"
#include "hw/bt.h"
#include "hw/watchdog.h"
#include "hw/smbios.h"
#include "hw/xen.h"
#include "bt-host.h"
#include "net.h"
#include "monitor.h"
#include "console.h"
#include "sysemu.h"
#include "gdbstub.h"
#include "qemu-timer.h"
#include "qemu-char.h"
#include "cache-utils.h"
#include "block.h"
#include "dma.h"
#include "audio/audio.h"
#include "migration.h"
#include "kvm.h"
#include "balloon.h"

#include "disas.h"

#include "exec-all.h"

#include "qemu_socket.h"

#if defined(CONFIG_SLIRP)
#include "libslirp.h"
#endif

//#define DEBUG_UNUSED_IOPORT
//#define DEBUG_IOPORT
//#define DEBUG_NET
//#define DEBUG_SLIRP


#ifdef DEBUG_IOPORT
#  define LOG_IOPORT(...) qemu_log_mask(CPU_LOG_IOPORT, ## __VA_ARGS__)
#else
#  define LOG_IOPORT(...) do { } while (0)
#endif

#define DEFAULT_RAM_SIZE 128

/* Max number of USB devices that can be specified on the commandline.  */
#define MAX_USB_CMDLINE 8

/* Max number of bluetooth switches on the commandline.  */
#define MAX_BT_CMDLINE 10

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
static int vga_ram_size;
enum vga_retrace_method vga_retrace_method = VGA_RETRACE_DUMB;
static DisplayState *display_state;
int nographic;
static int curses;
static int sdl;
const char* keyboard_layout = NULL;
int64_t ticks_per_sec;
ram_addr_t ram_size;
int nb_nics;
NICInfo nd_table[MAX_NICS];
int vm_running;
static int autostart;
static int rtc_utc = 1;
static int rtc_date_offset = -1; /* -1 means no change */
int cirrus_vga_enabled = 1;
int std_vga_enabled = 0;
int vmsvga_enabled = 0;
int xenfb_enabled = 0;
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
#ifdef CONFIG_SDL
static int no_frame = 0;
#endif
int no_quit = 0;
CharDriverState *serial_hds[MAX_SERIAL_PORTS];
CharDriverState *parallel_hds[MAX_PARALLEL_PORTS];
CharDriverState *virtcon_hds[MAX_VIRTIO_CONSOLES];
#ifdef TARGET_I386
int win2k_install_hack = 0;
int rtc_td_hack = 0;
#endif
int usb_enabled = 0;
int singlestep = 0;
int smp_cpus = 1;
const char *vnc_display;
int acpi_enabled = 1;
int no_hpet = 0;
int fd_bootchk = 1;
int no_reboot = 0;
int no_shutdown = 0;
int cursor_hide = 1;
int graphic_rotate = 0;
#ifndef _WIN32
int daemonize = 0;
#endif
WatchdogTimerModel *watchdog = NULL;
int watchdog_action = WDT_RESET;
const char *option_rom[MAX_OPTION_ROMS];
int nb_option_roms;
int semihosting_enabled = 0;
#ifdef TARGET_ARM
int old_param = 0;
#endif
const char *qemu_name;
int alt_grab = 0;
#if defined(TARGET_SPARC) || defined(TARGET_PPC)
unsigned int nb_prom_envs = 0;
const char *prom_envs[MAX_PROM_ENVS];
#endif
int nb_drives_opt;
struct drive_opt drives_opt[MAX_DRIVES];

int nb_numa_nodes;
uint64_t node_mem[MAX_NODES];
uint64_t node_cpumask[MAX_NODES];

static CPUState *cur_cpu;
static CPUState *next_cpu;
static int timer_alarm_pending = 1;
/* Conversion factor from emulated instructions to virtual clock ticks.  */
static int icount_time_shift;
/* Arbitrarily pick 1MIPS as the minimum allowable speed.  */
#define MAX_ICOUNT_SHIFT 10
/* Compensate for varying guest execution speed.  */
static int64_t qemu_icount_bias;
static QEMUTimer *icount_rt_timer;
static QEMUTimer *icount_vm_timer;
static QEMUTimer *nographic_timer;

uint8_t qemu_uuid[16];

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

        ioport_opaque[i] = NULL;
    }
}

/***********************************************************/

void cpu_outb(CPUState *env, int addr, int val)
{
    LOG_IOPORT("outb: %04x %02x\n", addr, val);
    ioport_write(0, addr, val);
#ifdef CONFIG_KQEMU
    if (env)
        env->last_io_time = cpu_get_time_fast();
#endif
}

void cpu_outw(CPUState *env, int addr, int val)
{
    LOG_IOPORT("outw: %04x %04x\n", addr, val);
    ioport_write(1, addr, val);
#ifdef CONFIG_KQEMU
    if (env)
        env->last_io_time = cpu_get_time_fast();
#endif
}

void cpu_outl(CPUState *env, int addr, int val)
{
    LOG_IOPORT("outl: %04x %08x\n", addr, val);
    ioport_write(2, addr, val);
#ifdef CONFIG_KQEMU
    if (env)
        env->last_io_time = cpu_get_time_fast();
#endif
}

int cpu_inb(CPUState *env, int addr)
{
    int val;
    val = ioport_read(0, addr);
    LOG_IOPORT("inb : %04x %02x\n", addr, val);
#ifdef CONFIG_KQEMU
    if (env)
        env->last_io_time = cpu_get_time_fast();
#endif
    return val;
}

int cpu_inw(CPUState *env, int addr)
{
    int val;
    val = ioport_read(1, addr);
    LOG_IOPORT("inw : %04x %04x\n", addr, val);
#ifdef CONFIG_KQEMU
    if (env)
        env->last_io_time = cpu_get_time_fast();
#endif
    return val;
}

int cpu_inl(CPUState *env, int addr)
{
    int val;
    val = ioport_read(2, addr);
    LOG_IOPORT("inl : %04x %08x\n", addr, val);
#ifdef CONFIG_KQEMU
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
 
/***************/
/* ballooning */

static QEMUBalloonEvent *qemu_balloon_event;
void *qemu_balloon_event_opaque;

void qemu_add_balloon_handler(QEMUBalloonEvent *func, void *opaque)
{
    qemu_balloon_event = func;
    qemu_balloon_event_opaque = opaque;
}

void qemu_balloon(ram_addr_t target)
{
    if (qemu_balloon_event)
        qemu_balloon_event(qemu_balloon_event_opaque, target);
}

ram_addr_t qemu_balloon_status(void)
{
    if (qemu_balloon_event)
        return qemu_balloon_event(qemu_balloon_event_opaque, 0);
    return 0;
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

void do_info_mice(Monitor *mon)
{
    QEMUPutMouseEntry *cursor;
    int index = 0;

    if (!qemu_put_mouse_event_head) {
        monitor_printf(mon, "No mouse devices connected\n");
        return;
    }

    monitor_printf(mon, "Mouse devices available:\n");
    cursor = qemu_put_mouse_event_head;
    while (cursor != NULL) {
        monitor_printf(mon, "%c Mouse #%d: %s\n",
                       (cursor == qemu_put_mouse_event_current ? '*' : ' '),
                       index, cursor->qemu_put_mouse_event_name);
        index++;
        cursor = cursor->next;
    }
}

void do_mouse_set(Monitor *mon, int index)
{
    QEMUPutMouseEntry *cursor;
    int i = 0;

    if (!qemu_put_mouse_event_head) {
        monitor_printf(mon, "No mouse devices connected\n");
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
        monitor_printf(mon, "Mouse at given index not found\n");
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
#if defined(__linux__) || (defined(__FreeBSD__) && __FreeBSD_version >= 500000) \
    || defined(__DragonFly__)
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
#if defined(__linux__) || (defined(__FreeBSD__) && __FreeBSD_version >= 500000) \
	|| defined(__DragonFly__)
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
    unsigned int period;
} alarm_win32_data = {0, -1};

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
    int count = ARRAY_SIZE(alarm_timers) - 1;
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
        if (use_icount)
            qemu_notify_event();
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

static void qemu_event_increment(void);

#ifdef _WIN32
static void CALLBACK host_alarm_handler(UINT uTimerID, UINT uMsg,
                                        DWORD_PTR dwUser, DWORD_PTR dw1,
                                        DWORD_PTR dw2)
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
        qemu_event_increment();
        alarm_timer->flags |= ALARM_FLAG_EXPIRED;

#ifndef CONFIG_IOTHREAD
        if (next_cpu) {
            /* stop the currently executing cpu because a timer occured */
            cpu_exit(next_cpu);
#ifdef CONFIG_KQEMU
            if (next_cpu->kqemu_enabled) {
                kqemu_cpu_interrupt(next_cpu);
            }
#endif
        }
#endif
        timer_alarm_pending = 1;
        qemu_notify_event();
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

/* Sets a specific flag */
static int fcntl_setfl(int fd, int flag)
{
    int flags;

    flags = fcntl(fd, F_GETFL);
    if (flags == -1)
        return -errno;

    if (fcntl(fd, F_SETFL, flags | flag) == -1)
        return -errno;

    return 0;
}

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
    fcntl_setfl(fd, O_ASYNC);
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

    t->priv = (void *)(long)host_timer;

    return 0;
}

static void dynticks_stop_timer(struct qemu_alarm_timer *t)
{
    timer_t host_timer = (timer_t)(long)t->priv;

    timer_delete(host_timer);
}

static void dynticks_rearm_timer(struct qemu_alarm_timer *t)
{
    timer_t host_timer = (timer_t)(long)t->priv;
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
        return -1;
    }

    return 0;
}

static void win32_stop_timer(struct qemu_alarm_timer *t)
{
    struct qemu_alarm_win32 *data = t->priv;

    timeKillEvent(data->timerId);
    timeEndPeriod(data->period);
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
        exit(1);
    }
}

#endif /* _WIN32 */

static int init_timer_alarm(void)
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
        err = -ENOENT;
        goto fail;
    }

    alarm_timer = t;

    return 0;

fail:
    return err;
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
#endif

const char *get_opt_name(char *buf, int buf_size, const char *p, char delim)
{
    char *q;

    q = buf;
    while (*p != '\0' && *p != delim) {
        if (q && (q - buf) < buf_size - 1)
            *q++ = *p;
        p++;
    }
    if (q)
        *q = '\0';

    return p;
}

const char *get_opt_value(char *buf, int buf_size, const char *p)
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

int get_param_value(char *buf, int buf_size,
                    const char *tag, const char *str)
{
    const char *p;
    char option[128];

    p = str;
    for(;;) {
        p = get_opt_name(option, sizeof(option), p, '=');
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

int check_params(const char * const *params, const char *str)
{
    int name_buf_size = 1;
    const char *p;
    char *name_buf;
    int i, len;
    int ret = 0;

    for (i = 0; params[i] != NULL; i++) {
        len = strlen(params[i]) + 1;
        if (len > name_buf_size) {
            name_buf_size = len;
        }
    }
    name_buf = qemu_malloc(name_buf_size);

    p = str;
    while (*p != '\0') {
        p = get_opt_name(name_buf, name_buf_size, p, '=');
        if (*p != '=') {
            ret = -1;
            break;
        }
        p++;
        for(i = 0; params[i] != NULL; i++)
            if (!strcmp(params[i], name_buf))
                break;
        if (params[i] == NULL) {
            ret = -1;
            break;
        }
        p = get_opt_value(NULL, 0, p);
        if (*p != ',')
            break;
        p++;
    }

    qemu_free(name_buf);
    return ret;
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

static struct HCIInfo *hci_init(const char *str)
{
    char *endp;
    struct bt_scatternet_s *vlan = 0;

    if (!strcmp(str, "null"))
        /* null */
        return &null_hci;
    else if (!strncmp(str, "host", 4) && (str[4] == '\0' || str[4] == ':'))
        /* host[:hciN] */
        return bt_host_hci(str[4] ? str + 5 : "hci0");
    else if (!strncmp(str, "hci", 3)) {
        /* hci[,vlan=n] */
        if (str[3]) {
            if (!strncmp(str + 3, ",vlan=", 6)) {
                vlan = qemu_find_bt_vlan(strtol(str + 9, &endp, 0));
                if (*endp)
                    vlan = 0;
            }
        } else
            vlan = qemu_find_bt_vlan(0);
        if (vlan)
           return bt_new_hci(vlan);
    }

    fprintf(stderr, "qemu: Unknown bluetooth HCI `%s'.\n", str);

    return 0;
}

static int bt_hci_parse(const char *str)
{
    struct HCIInfo *hci;
    bdaddr_t bdaddr;

    if (nb_hcis >= MAX_NICS) {
        fprintf(stderr, "qemu: Too many bluetooth HCIs (max %i).\n", MAX_NICS);
        return -1;
    }

    hci = hci_init(str);
    if (!hci)
        return -1;

    bdaddr.b[0] = 0x52;
    bdaddr.b[1] = 0x54;
    bdaddr.b[2] = 0x00;
    bdaddr.b[3] = 0x12;
    bdaddr.b[4] = 0x34;
    bdaddr.b[5] = 0x56 + nb_hcis;
    hci->bdaddr_set(hci, bdaddr.b);

    hci_table[nb_hcis++] = hci;

    return 0;
}

static void bt_vhci_add(int vlan_id)
{
    struct bt_scatternet_s *vlan = qemu_find_bt_vlan(vlan_id);

    if (!vlan->slave)
        fprintf(stderr, "qemu: warning: adding a VHCI to "
                        "an empty scatternet %i\n", vlan_id);

    bt_vhci_init(bt_new_hci(vlan));
}

static struct bt_device_s *bt_device_add(const char *opt)
{
    struct bt_scatternet_s *vlan;
    int vlan_id = 0;
    char *endp = strstr(opt, ",vlan=");
    int len = (endp ? endp - opt : strlen(opt)) + 1;
    char devname[10];

    pstrcpy(devname, MIN(sizeof(devname), len), opt);

    if (endp) {
        vlan_id = strtol(endp + 6, &endp, 0);
        if (*endp) {
            fprintf(stderr, "qemu: unrecognised bluetooth vlan Id\n");
            return 0;
        }
    }

    vlan = qemu_find_bt_vlan(vlan_id);

    if (!vlan->slave)
        fprintf(stderr, "qemu: warning: adding a slave device to "
                        "an empty scatternet %i\n", vlan_id);

    if (!strcmp(devname, "keyboard"))
        return bt_keyboard_init(vlan);

    fprintf(stderr, "qemu: unsupported bluetooth device `%s'\n", devname);
    return 0;
}

static int bt_parse(const char *opt)
{
    const char *endp, *p;
    int vlan;

    if (strstart(opt, "hci", &endp)) {
        if (!*endp || *endp == ',') {
            if (*endp)
                if (!strstart(endp, ",vlan=", 0))
                    opt = endp + 1;

            return bt_hci_parse(opt);
       }
    } else if (strstart(opt, "vhci", &endp)) {
        if (!*endp || *endp == ',') {
            if (*endp) {
                if (strstart(endp, ",vlan=", &p)) {
                    vlan = strtol(p, (char **) &endp, 0);
                    if (*endp) {
                        fprintf(stderr, "qemu: bad scatternet '%s'\n", p);
                        return 1;
                    }
                } else {
                    fprintf(stderr, "qemu: bad parameter '%s'\n", endp + 1);
                    return 1;
                }
            } else
                vlan = 0;

            bt_vhci_add(vlan);
            return 0;
        }
    } else if (strstart(opt, "device:", &endp))
        return !bt_device_add(endp);

    fprintf(stderr, "qemu: bad bluetooth parameter '%s'\n", opt);
    return 1;
}

/***********************************************************/
/* QEMU Block devices */

#define HD_ALIAS "index=%d,media=disk"
#define CDROM_ALIAS "index=2,media=cdrom"
#define FD_ALIAS "index=%d,if=floppy"
#define PFLASH_ALIAS "if=pflash"
#define MTD_ALIAS "if=mtd"
#define SD_ALIAS "index=0,if=sd"

static int drive_opt_get_free_idx(void)
{
    int index;

    for (index = 0; index < MAX_DRIVES; index++)
        if (!drives_opt[index].used) {
            drives_opt[index].used = 1;
            return index;
        }

    return -1;
}

static int drive_get_free_idx(void)
{
    int index;

    for (index = 0; index < MAX_DRIVES; index++)
        if (!drives_table[index].used) {
            drives_table[index].used = 1;
            return index;
        }

    return -1;
}

int drive_add(const char *file, const char *fmt, ...)
{
    va_list ap;
    int index = drive_opt_get_free_idx();

    if (nb_drives_opt >= MAX_DRIVES || index == -1) {
        fprintf(stderr, "qemu: too many drives\n");
        return -1;
    }

    drives_opt[index].file = file;
    va_start(ap, fmt);
    vsnprintf(drives_opt[index].opt,
              sizeof(drives_opt[0].opt), fmt, ap);
    va_end(ap);

    nb_drives_opt++;
    return index;
}

void drive_remove(int index)
{
    drives_opt[index].used = 0;
    nb_drives_opt--;
}

int drive_get_index(BlockInterfaceType type, int bus, int unit)
{
    int index;

    /* seek interface, bus and unit */

    for (index = 0; index < MAX_DRIVES; index++)
        if (drives_table[index].type == type &&
	    drives_table[index].bus == bus &&
	    drives_table[index].unit == unit &&
	    drives_table[index].used)
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

const char *drive_get_serial(BlockDriverState *bdrv)
{
    int index;

    for (index = 0; index < nb_drives; index++)
        if (drives_table[index].bdrv == bdrv)
            return drives_table[index].serial;

    return "\0";
}

BlockInterfaceErrorAction drive_get_onerror(BlockDriverState *bdrv)
{
    int index;

    for (index = 0; index < nb_drives; index++)
        if (drives_table[index].bdrv == bdrv)
            return drives_table[index].onerror;

    return BLOCK_ERR_STOP_ENOSPC;
}

static void bdrv_format_print(void *opaque, const char *name)
{
    fprintf(stderr, " %s", name);
}

void drive_uninit(BlockDriverState *bdrv)
{
    int i;

    for (i = 0; i < MAX_DRIVES; i++)
        if (drives_table[i].bdrv == bdrv) {
            drives_table[i].bdrv = NULL;
            drives_table[i].used = 0;
            drive_remove(drives_table[i].drive_opt_idx);
            nb_drives--;
            break;
        }
}

int drive_init(struct drive_opt *arg, int snapshot, void *opaque)
{
    char buf[128];
    char file[1024];
    char devname[128];
    char serial[21];
    const char *mediastr = "";
    BlockInterfaceType type;
    enum { MEDIA_DISK, MEDIA_CDROM } media;
    int bus_id, unit_id;
    int cyls, heads, secs, translation;
    BlockDriverState *bdrv;
    BlockDriver *drv = NULL;
    QEMUMachine *machine = opaque;
    int max_devs;
    int index;
    int cache;
    int bdrv_flags, onerror;
    int drives_table_idx;
    char *str = arg->opt;
    static const char * const params[] = { "bus", "unit", "if", "index",
                                           "cyls", "heads", "secs", "trans",
                                           "media", "snapshot", "file",
                                           "cache", "format", "serial", "werror",
                                           NULL };

    if (check_params(params, str) < 0) {
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
    cache = 3;

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
        } else if (!strcmp(buf, "virtio")) {
            type = IF_VIRTIO;
            max_devs = 0;
	} else if (!strcmp(buf, "xen")) {
	    type = IF_XEN;
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
        if (!strcmp(buf, "off") || !strcmp(buf, "none"))
            cache = 0;
        else if (!strcmp(buf, "writethrough"))
            cache = 1;
        else if (!strcmp(buf, "writeback"))
            cache = 2;
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

    if (!get_param_value(serial, sizeof(serial), "serial", str))
	    memset(serial, 0,  sizeof(serial));

    onerror = BLOCK_ERR_STOP_ENOSPC;
    if (get_param_value(buf, sizeof(serial), "werror", str)) {
        if (type != IF_IDE && type != IF_SCSI && type != IF_VIRTIO) {
            fprintf(stderr, "werror is no supported by this format\n");
            return -1;
        }
        if (!strcmp(buf, "ignore"))
            onerror = BLOCK_ERR_IGNORE;
        else if (!strcmp(buf, "enospc"))
            onerror = BLOCK_ERR_STOP_ENOSPC;
        else if (!strcmp(buf, "stop"))
            onerror = BLOCK_ERR_STOP_ANY;
        else if (!strcmp(buf, "report"))
            onerror = BLOCK_ERR_REPORT;
        else {
            fprintf(stderr, "qemu: '%s' invalid write error action\n", buf);
            return -1;
        }
    }

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
        return -2;

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
    drives_table_idx = drive_get_free_idx();
    drives_table[drives_table_idx].bdrv = bdrv;
    drives_table[drives_table_idx].type = type;
    drives_table[drives_table_idx].bus = bus_id;
    drives_table[drives_table_idx].unit = unit_id;
    drives_table[drives_table_idx].onerror = onerror;
    drives_table[drives_table_idx].drive_opt_idx = arg - drives_opt;
    strncpy(drives_table[drives_table_idx].serial, serial, sizeof(serial));
    nb_drives++;

    switch(type) {
    case IF_IDE:
    case IF_SCSI:
    case IF_XEN:
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
    case IF_VIRTIO:
        break;
    }
    if (!file[0])
        return -2;
    bdrv_flags = 0;
    if (snapshot) {
        bdrv_flags |= BDRV_O_SNAPSHOT;
        cache = 2; /* always use write-back with snapshot */
    }
    if (cache == 0) /* no caching */
        bdrv_flags |= BDRV_O_NOCACHE;
    else if (cache == 2) /* write-back */
        bdrv_flags |= BDRV_O_CACHE_WB;
    else if (cache == 3) /* not specified */
        bdrv_flags |= BDRV_O_CACHE_DEF;
    if (bdrv_open2(bdrv, file, bdrv_flags, drv) < 0) {
        fprintf(stderr, "qemu: could not open disk image %s\n",
                        file);
        return -1;
    }
    if (bdrv_key_required(bdrv))
        autostart = 0;
    return drives_table_idx;
}

static void numa_add(const char *optarg)
{
    char option[128];
    char *endptr;
    unsigned long long value, endvalue;
    int nodenr;

    optarg = get_opt_name(option, 128, optarg, ',') + 1;
    if (!strcmp(option, "node")) {
        if (get_param_value(option, 128, "nodeid", optarg) == 0) {
            nodenr = nb_numa_nodes;
        } else {
            nodenr = strtoull(option, NULL, 10);
        }

        if (get_param_value(option, 128, "mem", optarg) == 0) {
            node_mem[nodenr] = 0;
        } else {
            value = strtoull(option, &endptr, 0);
            switch (*endptr) {
            case 0: case 'M': case 'm':
                value <<= 20;
                break;
            case 'G': case 'g':
                value <<= 30;
                break;
            }
            node_mem[nodenr] = value;
        }
        if (get_param_value(option, 128, "cpus", optarg) == 0) {
            node_cpumask[nodenr] = 0;
        } else {
            value = strtoull(option, &endptr, 10);
            if (value >= 64) {
                value = 63;
                fprintf(stderr, "only 64 CPUs in NUMA mode supported.\n");
            } else {
                if (*endptr == '-') {
                    endvalue = strtoull(endptr+1, &endptr, 10);
                    if (endvalue >= 63) {
                        endvalue = 62;
                        fprintf(stderr,
                            "only 63 CPUs in NUMA mode supported.\n");
                    }
                    value = (1 << (endvalue + 1)) - (1 << value);
                } else {
                    value = 1 << value;
                }
            }
            node_cpumask[nodenr] = value;
        }
        nb_numa_nodes++;
    }
    return;
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

static void usb_msd_password_cb(void *opaque, int err)
{
    USBDevice *dev = opaque;

    if (!err)
        usb_device_add_dev(dev);
    else
        dev->handle_destroy(dev);
}

static int usb_device_add(const char *devname, int is_hotplug)
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
        BlockDriverState *bs;

        dev = usb_msd_init(p);
        if (!dev)
            return -1;
        bs = usb_msd_get_bdrv(dev);
        if (bdrv_key_required(bs)) {
            autostart = 0;
            if (is_hotplug) {
                monitor_read_bdrv_key_start(cur_mon, bs, usb_msd_password_cb,
                                            dev);
                return 0;
            }
        }
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
    } else if (!strcmp(devname, "bt") || strstart(devname, "bt:", &p)) {
        dev = usb_bt_init(devname[2] ? hci_init(p) :
                        bt_new_hci(qemu_find_bt_vlan(0)));
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

void do_usb_add(Monitor *mon, const char *devname)
{
    usb_device_add(devname, 1);
}

void do_usb_del(Monitor *mon, const char *devname)
{
    usb_device_del(devname);
}

void usb_info(Monitor *mon)
{
    USBDevice *dev;
    USBPort *port;
    const char *speed_str;

    if (!usb_enabled) {
        monitor_printf(mon, "USB support not enabled\n");
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
        monitor_printf(mon, "  Device %d.%d, Speed %s Mb/s, Product %s\n",
                       0, dev->addr, speed_str, dev->devname);
    }
}

/***********************************************************/
/* PCMCIA/Cardbus */

static struct pcmcia_socket_entry_s {
    PCMCIASocket *socket;
    struct pcmcia_socket_entry_s *next;
} *pcmcia_sockets = 0;

void pcmcia_socket_register(PCMCIASocket *socket)
{
    struct pcmcia_socket_entry_s *entry;

    entry = qemu_malloc(sizeof(struct pcmcia_socket_entry_s));
    entry->socket = socket;
    entry->next = pcmcia_sockets;
    pcmcia_sockets = entry;
}

void pcmcia_socket_unregister(PCMCIASocket *socket)
{
    struct pcmcia_socket_entry_s *entry, **ptr;

    ptr = &pcmcia_sockets;
    for (entry = *ptr; entry; ptr = &entry->next, entry = *ptr)
        if (entry->socket == socket) {
            *ptr = entry->next;
            qemu_free(entry);
        }
}

void pcmcia_info(Monitor *mon)
{
    struct pcmcia_socket_entry_s *iter;

    if (!pcmcia_sockets)
        monitor_printf(mon, "No PCMCIA sockets\n");

    for (iter = pcmcia_sockets; iter; iter = iter->next)
        monitor_printf(mon, "%s: %s\n", iter->socket->slot_string,
                       iter->socket->attached ? iter->socket->card_string :
                       "Empty");
}

/***********************************************************/
/* register display */

struct DisplayAllocator default_allocator = {
    defaultallocator_create_displaysurface,
    defaultallocator_resize_displaysurface,
    defaultallocator_free_displaysurface
};

void register_displaystate(DisplayState *ds)
{
    DisplayState **s;
    s = &display_state;
    while (*s != NULL)
        s = &(*s)->next;
    ds->next = NULL;
    *s = ds;
}

DisplayState *get_displaystate(void)
{
    return display_state;
}

DisplayAllocator *register_displayallocator(DisplayState *ds, DisplayAllocator *da)
{
    if(ds->allocator ==  &default_allocator) ds->allocator = da;
    return ds->allocator;
}

/* dumb display */

static void dumb_display_init(void)
{
    DisplayState *ds = qemu_mallocz(sizeof(DisplayState));
    ds->allocator = &default_allocator;
    ds->surface = qemu_create_displaysurface(ds, 640, 480);
    register_displaystate(ds);
}

/***********************************************************/
/* I/O handling */

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

#ifdef _WIN32
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

    if (qemu_file_has_error(f))
        return -EIO;

    return 0;
}

static int ram_load_v1(QEMUFile *f, void *opaque)
{
    int ret;
    ram_addr_t i;

    if (qemu_get_be32(f) != last_ram_offset)
        return -EINVAL;
    for(i = 0; i < last_ram_offset; i+= TARGET_PAGE_SIZE) {
        ret = ram_get_page(f, qemu_get_ram_ptr(i), TARGET_PAGE_SIZE);
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

    while (addr < last_ram_offset) {
        if (cpu_physical_memory_get_dirty(current_addr, MIGRATION_DIRTY_FLAG)) {
            uint8_t *p;

            cpu_physical_memory_reset_dirty(current_addr,
                                            current_addr + TARGET_PAGE_SIZE,
                                            MIGRATION_DIRTY_FLAG);

            p = qemu_get_ram_ptr(current_addr);

            if (is_dup_page(p, *p)) {
                qemu_put_be64(f, current_addr | RAM_SAVE_FLAG_COMPRESS);
                qemu_put_byte(f, *p);
            } else {
                qemu_put_be64(f, current_addr | RAM_SAVE_FLAG_PAGE);
                qemu_put_buffer(f, p, TARGET_PAGE_SIZE);
            }

            found = 1;
            break;
        }
        addr += TARGET_PAGE_SIZE;
        current_addr = (saved_addr + addr) % last_ram_offset;
    }

    return found;
}

static ram_addr_t ram_save_threshold = 10;

static ram_addr_t ram_save_remaining(void)
{
    ram_addr_t addr;
    ram_addr_t count = 0;

    for (addr = 0; addr < last_ram_offset; addr += TARGET_PAGE_SIZE) {
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
        for (addr = 0; addr < last_ram_offset; addr += TARGET_PAGE_SIZE) {
            if (!cpu_physical_memory_get_dirty(addr, MIGRATION_DIRTY_FLAG))
                cpu_physical_memory_set_dirty(addr);
        }
        
        /* Enable dirty memory tracking */
        cpu_physical_memory_set_dirty_tracking(1);

        qemu_put_be64(f, last_ram_offset | RAM_SAVE_FLAG_MEM_SIZE);
    }

    while (!qemu_file_rate_limit(f)) {
        int ret;

        ret = ram_save_block(f);
        if (ret == 0) /* no more blocks */
            break;
    }

    /* try transferring iterative blocks of memory */

    if (stage == 3) {

        /* flush all remaining blocks regardless of rate limiting */
        while (ram_save_block(f) != 0);
        cpu_physical_memory_set_dirty_tracking(0);
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
    for(i = 0; i < last_ram_offset; i+= BDRV_HASH_BLOCK_SIZE) {
        if (ram_decompress_buf(s, buf, 1) < 0) {
            fprintf(stderr, "Error while reading ram block header\n");
            goto error;
        }
        if (buf[0] == 0) {
            if (ram_decompress_buf(s, qemu_get_ram_ptr(i),
                                   BDRV_HASH_BLOCK_SIZE) < 0) {
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
        if (qemu_get_be32(f) != last_ram_offset)
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
            if (addr != last_ram_offset)
                return -EINVAL;
        }

        if (flags & RAM_SAVE_FLAG_FULL) {
            if (ram_load_dead(f, opaque) < 0)
                return -EINVAL;
        }
        
        if (flags & RAM_SAVE_FLAG_COMPRESS) {
            uint8_t ch = qemu_get_byte(f);
            memset(qemu_get_ram_ptr(addr), ch, TARGET_PAGE_SIZE);
        } else if (flags & RAM_SAVE_FLAG_PAGE)
            qemu_get_buffer(f, qemu_get_ram_ptr(addr), TARGET_PAGE_SIZE);
    } while (!(flags & RAM_SAVE_FLAG_EOS));

    return 0;
}

void qemu_service_io(void)
{
    qemu_notify_event();
}

/***********************************************************/
/* bottom halves (can be seen as timers which expire ASAP) */

struct QEMUBH {
    QEMUBHFunc *cb;
    void *opaque;
    int scheduled;
    int idle;
    int deleted;
    QEMUBH *next;
};

static QEMUBH *first_bh = NULL;

QEMUBH *qemu_bh_new(QEMUBHFunc *cb, void *opaque)
{
    QEMUBH *bh;
    bh = qemu_mallocz(sizeof(QEMUBH));
    bh->cb = cb;
    bh->opaque = opaque;
    bh->next = first_bh;
    first_bh = bh;
    return bh;
}

int qemu_bh_poll(void)
{
    QEMUBH *bh, **bhp;
    int ret;

    ret = 0;
    for (bh = first_bh; bh; bh = bh->next) {
        if (!bh->deleted && bh->scheduled) {
            bh->scheduled = 0;
            if (!bh->idle)
                ret = 1;
            bh->idle = 0;
            bh->cb(bh->opaque);
        }
    }

    /* remove deleted bhs */
    bhp = &first_bh;
    while (*bhp) {
        bh = *bhp;
        if (bh->deleted) {
            *bhp = bh->next;
            qemu_free(bh);
        } else
            bhp = &bh->next;
    }

    return ret;
}

void qemu_bh_schedule_idle(QEMUBH *bh)
{
    if (bh->scheduled)
        return;
    bh->scheduled = 1;
    bh->idle = 1;
}

void qemu_bh_schedule(QEMUBH *bh)
{
    if (bh->scheduled)
        return;
    bh->scheduled = 1;
    bh->idle = 0;
    /* stop the currently executing CPU to execute the BH ASAP */
    qemu_notify_event();
}

void qemu_bh_cancel(QEMUBH *bh)
{
    bh->scheduled = 0;
}

void qemu_bh_delete(QEMUBH *bh)
{
    bh->scheduled = 0;
    bh->deleted = 1;
}

static void qemu_bh_update_timeout(int *timeout)
{
    QEMUBH *bh;

    for (bh = first_bh; bh; bh = bh->next) {
        if (!bh->deleted && bh->scheduled) {
            if (bh->idle) {
                /* idle bottom halves will be polled at least
                 * every 10ms */
                *timeout = MIN(10, *timeout);
            } else {
                /* non-idle bottom halves will be executed
                 * immediately */
                *timeout = 0;
                break;
            }
        }
    }
}

/***********************************************************/
/* machine registration */

static QEMUMachine *first_machine = NULL;
QEMUMachine *current_machine = NULL;

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
    uint64_t interval = GUI_REFRESH_INTERVAL;
    DisplayState *ds = opaque;
    DisplayChangeListener *dcl = ds->listeners;

    dpy_refresh(ds);

    while (dcl != NULL) {
        if (dcl->gui_timer_interval &&
            dcl->gui_timer_interval < interval)
            interval = dcl->gui_timer_interval;
        dcl = dcl->next;
    }
    qemu_mod_timer(ds->gui_timer, interval + qemu_get_clock(rt_clock));
}

static void nographic_update(void *opaque)
{
    uint64_t interval = GUI_REFRESH_INTERVAL;

    qemu_mod_timer(nographic_timer, interval + qemu_get_clock(rt_clock));
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

static void vm_state_notify(int running, int reason)
{
    VMChangeStateEntry *e;

    for (e = vm_change_state_head.lh_first; e; e = e->entries.le_next) {
        e->cb(e->opaque, running, reason);
    }
}

static void resume_all_vcpus(void);
static void pause_all_vcpus(void);

void vm_start(void)
{
    if (!vm_running) {
        cpu_enable_ticks();
        vm_running = 1;
        vm_state_notify(1, 0);
        qemu_rearm_alarm_timer(alarm_timer);
        resume_all_vcpus();
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
static int debug_requested;
static int vmstop_requested;

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

static int qemu_debug_requested(void)
{
    int r = debug_requested;
    debug_requested = 0;
    return r;
}

static int qemu_vmstop_requested(void)
{
    int r = vmstop_requested;
    vmstop_requested = 0;
    return r;
}

static void do_vm_stop(int reason)
{
    if (vm_running) {
        cpu_disable_ticks();
        vm_running = 0;
        pause_all_vcpus();
        vm_state_notify(0, reason);
    }
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
    if (kvm_enabled())
        kvm_sync_vcpus();
}

void qemu_system_reset_request(void)
{
    if (no_reboot) {
        shutdown_requested = 1;
    } else {
        reset_requested = 1;
    }
    qemu_notify_event();
}

void qemu_system_shutdown_request(void)
{
    shutdown_requested = 1;
    qemu_notify_event();
}

void qemu_system_powerdown_request(void)
{
    powerdown_requested = 1;
    qemu_notify_event();
}

#ifdef CONFIG_IOTHREAD
static void qemu_system_vmstop_request(int reason)
{
    vmstop_requested = reason;
    qemu_notify_event();
}
#endif

#ifndef _WIN32
static int io_thread_fd = -1;

static void qemu_event_increment(void)
{
    static const char byte = 0;

    if (io_thread_fd == -1)
        return;

    write(io_thread_fd, &byte, sizeof(byte));
}

static void qemu_event_read(void *opaque)
{
    int fd = (unsigned long)opaque;
    ssize_t len;

    /* Drain the notify pipe */
    do {
        char buffer[512];
        len = read(fd, buffer, sizeof(buffer));
    } while ((len == -1 && errno == EINTR) || len > 0);
}

static int qemu_event_init(void)
{
    int err;
    int fds[2];

    err = pipe(fds);
    if (err == -1)
        return -errno;

    err = fcntl_setfl(fds[0], O_NONBLOCK);
    if (err < 0)
        goto fail;

    err = fcntl_setfl(fds[1], O_NONBLOCK);
    if (err < 0)
        goto fail;

    qemu_set_fd_handler2(fds[0], NULL, qemu_event_read, NULL,
                         (void *)(unsigned long)fds[0]);

    io_thread_fd = fds[1];
    return 0;

fail:
    close(fds[0]);
    close(fds[1]);
    return err;
}
#else
HANDLE qemu_event_handle;

static void dummy_event_handler(void *opaque)
{
}

static int qemu_event_init(void)
{
    qemu_event_handle = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!qemu_event_handle) {
        perror("Failed CreateEvent");
        return -1;
    }
    qemu_add_wait_object(qemu_event_handle, dummy_event_handler, NULL);
    return 0;
}

static void qemu_event_increment(void)
{
    SetEvent(qemu_event_handle);
}
#endif

static int cpu_can_run(CPUState *env)
{
    if (env->stop)
        return 0;
    if (env->stopped)
        return 0;
    return 1;
}

#ifndef CONFIG_IOTHREAD
static int qemu_init_main_loop(void)
{
    return qemu_event_init();
}

void qemu_init_vcpu(void *_env)
{
    CPUState *env = _env;

    if (kvm_enabled())
        kvm_init_vcpu(env);
    return;
}

int qemu_cpu_self(void *env)
{
    return 1;
}

static void resume_all_vcpus(void)
{
}

static void pause_all_vcpus(void)
{
}

void qemu_cpu_kick(void *env)
{
    return;
}

void qemu_notify_event(void)
{
    CPUState *env = cpu_single_env;

    if (env) {
        cpu_exit(env);
#ifdef USE_KQEMU
        if (env->kqemu_enabled)
            kqemu_cpu_interrupt(env);
#endif
     }
}

#define qemu_mutex_lock_iothread() do { } while (0)
#define qemu_mutex_unlock_iothread() do { } while (0)

void vm_stop(int reason)
{
    do_vm_stop(reason);
}

#else /* CONFIG_IOTHREAD */

#include "qemu-thread.h"

QemuMutex qemu_global_mutex;
static QemuMutex qemu_fair_mutex;

static QemuThread io_thread;

static QemuThread *tcg_cpu_thread;
static QemuCond *tcg_halt_cond;

static int qemu_system_ready;
/* cpu creation */
static QemuCond qemu_cpu_cond;
/* system init */
static QemuCond qemu_system_cond;
static QemuCond qemu_pause_cond;

static void block_io_signals(void);
static void unblock_io_signals(void);
static int tcg_has_work(void);

static int qemu_init_main_loop(void)
{
    int ret;

    ret = qemu_event_init();
    if (ret)
        return ret;

    qemu_cond_init(&qemu_pause_cond);
    qemu_mutex_init(&qemu_fair_mutex);
    qemu_mutex_init(&qemu_global_mutex);
    qemu_mutex_lock(&qemu_global_mutex);

    unblock_io_signals();
    qemu_thread_self(&io_thread);

    return 0;
}

static void qemu_wait_io_event(CPUState *env)
{
    while (!tcg_has_work())
        qemu_cond_timedwait(env->halt_cond, &qemu_global_mutex, 1000);

    qemu_mutex_unlock(&qemu_global_mutex);

    /*
     * Users of qemu_global_mutex can be starved, having no chance
     * to acquire it since this path will get to it first.
     * So use another lock to provide fairness.
     */
    qemu_mutex_lock(&qemu_fair_mutex);
    qemu_mutex_unlock(&qemu_fair_mutex);

    qemu_mutex_lock(&qemu_global_mutex);
    if (env->stop) {
        env->stop = 0;
        env->stopped = 1;
        qemu_cond_signal(&qemu_pause_cond);
    }
}

static int qemu_cpu_exec(CPUState *env);

static void *kvm_cpu_thread_fn(void *arg)
{
    CPUState *env = arg;

    block_io_signals();
    qemu_thread_self(env->thread);

    /* signal CPU creation */
    qemu_mutex_lock(&qemu_global_mutex);
    env->created = 1;
    qemu_cond_signal(&qemu_cpu_cond);

    /* and wait for machine initialization */
    while (!qemu_system_ready)
        qemu_cond_timedwait(&qemu_system_cond, &qemu_global_mutex, 100);

    while (1) {
        if (cpu_can_run(env))
            qemu_cpu_exec(env);
        qemu_wait_io_event(env);
    }

    return NULL;
}

static void tcg_cpu_exec(void);

static void *tcg_cpu_thread_fn(void *arg)
{
    CPUState *env = arg;

    block_io_signals();
    qemu_thread_self(env->thread);

    /* signal CPU creation */
    qemu_mutex_lock(&qemu_global_mutex);
    for (env = first_cpu; env != NULL; env = env->next_cpu)
        env->created = 1;
    qemu_cond_signal(&qemu_cpu_cond);

    /* and wait for machine initialization */
    while (!qemu_system_ready)
        qemu_cond_timedwait(&qemu_system_cond, &qemu_global_mutex, 100);

    while (1) {
        tcg_cpu_exec();
        qemu_wait_io_event(cur_cpu);
    }

    return NULL;
}

void qemu_cpu_kick(void *_env)
{
    CPUState *env = _env;
    qemu_cond_broadcast(env->halt_cond);
    if (kvm_enabled())
        qemu_thread_signal(env->thread, SIGUSR1);
}

int qemu_cpu_self(void *env)
{
    return (cpu_single_env != NULL);
}

static void cpu_signal(int sig)
{
    if (cpu_single_env)
        cpu_exit(cpu_single_env);
}

static void block_io_signals(void)
{
    sigset_t set;
    struct sigaction sigact;

    sigemptyset(&set);
    sigaddset(&set, SIGUSR2);
    sigaddset(&set, SIGIO);
    sigaddset(&set, SIGALRM);
    pthread_sigmask(SIG_BLOCK, &set, NULL);

    sigemptyset(&set);
    sigaddset(&set, SIGUSR1);
    pthread_sigmask(SIG_UNBLOCK, &set, NULL);

    memset(&sigact, 0, sizeof(sigact));
    sigact.sa_handler = cpu_signal;
    sigaction(SIGUSR1, &sigact, NULL);
}

static void unblock_io_signals(void)
{
    sigset_t set;

    sigemptyset(&set);
    sigaddset(&set, SIGUSR2);
    sigaddset(&set, SIGIO);
    sigaddset(&set, SIGALRM);
    pthread_sigmask(SIG_UNBLOCK, &set, NULL);

    sigemptyset(&set);
    sigaddset(&set, SIGUSR1);
    pthread_sigmask(SIG_BLOCK, &set, NULL);
}

static void qemu_signal_lock(unsigned int msecs)
{
    qemu_mutex_lock(&qemu_fair_mutex);

    while (qemu_mutex_trylock(&qemu_global_mutex)) {
        qemu_thread_signal(tcg_cpu_thread, SIGUSR1);
        if (!qemu_mutex_timedlock(&qemu_global_mutex, msecs))
            break;
    }
    qemu_mutex_unlock(&qemu_fair_mutex);
}

static void qemu_mutex_lock_iothread(void)
{
    if (kvm_enabled()) {
        qemu_mutex_lock(&qemu_fair_mutex);
        qemu_mutex_lock(&qemu_global_mutex);
        qemu_mutex_unlock(&qemu_fair_mutex);
    } else
        qemu_signal_lock(100);
}

static void qemu_mutex_unlock_iothread(void)
{
    qemu_mutex_unlock(&qemu_global_mutex);
}

static int all_vcpus_paused(void)
{
    CPUState *penv = first_cpu;

    while (penv) {
        if (!penv->stopped)
            return 0;
        penv = (CPUState *)penv->next_cpu;
    }

    return 1;
}

static void pause_all_vcpus(void)
{
    CPUState *penv = first_cpu;

    while (penv) {
        penv->stop = 1;
        qemu_thread_signal(penv->thread, SIGUSR1);
        qemu_cpu_kick(penv);
        penv = (CPUState *)penv->next_cpu;
    }

    while (!all_vcpus_paused()) {
        qemu_cond_timedwait(&qemu_pause_cond, &qemu_global_mutex, 100);
        penv = first_cpu;
        while (penv) {
            qemu_thread_signal(penv->thread, SIGUSR1);
            penv = (CPUState *)penv->next_cpu;
        }
    }
}

static void resume_all_vcpus(void)
{
    CPUState *penv = first_cpu;

    while (penv) {
        penv->stop = 0;
        penv->stopped = 0;
        qemu_thread_signal(penv->thread, SIGUSR1);
        qemu_cpu_kick(penv);
        penv = (CPUState *)penv->next_cpu;
    }
}

static void tcg_init_vcpu(void *_env)
{
    CPUState *env = _env;
    /* share a single thread for all cpus with TCG */
    if (!tcg_cpu_thread) {
        env->thread = qemu_mallocz(sizeof(QemuThread));
        env->halt_cond = qemu_mallocz(sizeof(QemuCond));
        qemu_cond_init(env->halt_cond);
        qemu_thread_create(env->thread, tcg_cpu_thread_fn, env);
        while (env->created == 0)
            qemu_cond_timedwait(&qemu_cpu_cond, &qemu_global_mutex, 100);
        tcg_cpu_thread = env->thread;
        tcg_halt_cond = env->halt_cond;
    } else {
        env->thread = tcg_cpu_thread;
        env->halt_cond = tcg_halt_cond;
    }
}

static void kvm_start_vcpu(CPUState *env)
{
    kvm_init_vcpu(env);
    env->thread = qemu_mallocz(sizeof(QemuThread));
    env->halt_cond = qemu_mallocz(sizeof(QemuCond));
    qemu_cond_init(env->halt_cond);
    qemu_thread_create(env->thread, kvm_cpu_thread_fn, env);
    while (env->created == 0)
        qemu_cond_timedwait(&qemu_cpu_cond, &qemu_global_mutex, 100);
}

void qemu_init_vcpu(void *_env)
{
    CPUState *env = _env;

    if (kvm_enabled())
        kvm_start_vcpu(env);
    else
        tcg_init_vcpu(env);
}

void qemu_notify_event(void)
{
    qemu_event_increment();
}

void vm_stop(int reason)
{
    QemuThread me;
    qemu_thread_self(&me);

    if (!qemu_thread_equal(&me, &io_thread)) {
        qemu_system_vmstop_request(reason);
        /*
         * FIXME: should not return to device code in case
         * vm_stop() has been requested.
         */
        if (cpu_single_env) {
            cpu_exit(cpu_single_env);
            cpu_single_env->stop = 1;
        }
        return;
    }
    do_vm_stop(reason);
}

#endif


#ifdef _WIN32
static void host_main_loop_wait(int *timeout)
{
    int ret, ret2, i;
    PollingEntry *pe;


    /* XXX: need to suppress polling by better using win32 events */
    ret = 0;
    for(pe = first_polling_entry; pe != NULL; pe = pe->next) {
        ret |= pe->func(pe->opaque);
    }
    if (ret == 0) {
        int err;
        WaitObjects *w = &wait_objects;

        ret = WaitForMultipleObjects(w->num, w->events, FALSE, *timeout);
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

    *timeout = 0;
}
#else
static void host_main_loop_wait(int *timeout)
{
}
#endif

void main_loop_wait(int timeout)
{
    IOHandlerRecord *ioh;
    fd_set rfds, wfds, xfds;
    int ret, nfds;
    struct timeval tv;

    qemu_bh_update_timeout(&timeout);

    host_main_loop_wait(&timeout);

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

    tv.tv_sec = timeout / 1000;
    tv.tv_usec = (timeout % 1000) * 1000;

#if defined(CONFIG_SLIRP)
    if (slirp_is_inited()) {
        slirp_select_fill(&nfds, &rfds, &wfds, &xfds);
    }
#endif
    qemu_mutex_unlock_iothread();
    ret = select(nfds + 1, &rfds, &wfds, &xfds, &tv);
    qemu_mutex_lock_iothread();
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
    if (slirp_is_inited()) {
        if (ret < 0) {
            FD_ZERO(&rfds);
            FD_ZERO(&wfds);
            FD_ZERO(&xfds);
        }
        slirp_select_poll(&rfds, &wfds, &xfds);
    }
#endif

    /* rearm timer, if not periodic */
    if (alarm_timer->flags & ALARM_FLAG_EXPIRED) {
        alarm_timer->flags &= ~ALARM_FLAG_EXPIRED;
        qemu_rearm_alarm_timer(alarm_timer);
    }

    /* vm time timers */
    if (vm_running) {
        if (!cur_cpu || likely(!(cur_cpu->singlestep_enabled & SSTEP_NOTIMER)))
            qemu_run_timers(&active_timers[QEMU_TIMER_VIRTUAL],
                qemu_get_clock(vm_clock));
    }

    /* real time timers */
    qemu_run_timers(&active_timers[QEMU_TIMER_REALTIME],
                    qemu_get_clock(rt_clock));

    /* Check bottom-halves last in case any of the earlier events triggered
       them.  */
    qemu_bh_poll();

}

static int qemu_cpu_exec(CPUState *env)
{
    int ret;
#ifdef CONFIG_PROFILER
    int64_t ti;
#endif

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
    return ret;
}

static void tcg_cpu_exec(void)
{
    int ret = 0;

    if (next_cpu == NULL)
        next_cpu = first_cpu;
    for (; next_cpu != NULL; next_cpu = next_cpu->next_cpu) {
        CPUState *env = cur_cpu = next_cpu;

        if (!vm_running)
            break;
        if (timer_alarm_pending) {
            timer_alarm_pending = 0;
            break;
        }
        if (cpu_can_run(env))
            ret = qemu_cpu_exec(env);
        if (ret == EXCP_DEBUG) {
            gdb_set_stop_cpu(env);
            debug_requested = 1;
            break;
        }
    }
}

static int cpu_has_work(CPUState *env)
{
    if (env->stop)
        return 1;
    if (env->stopped)
        return 0;
    if (!env->halted)
        return 1;
    if (qemu_cpu_has_work(env))
        return 1;
    return 0;
}

static int tcg_has_work(void)
{
    CPUState *env;

    for (env = first_cpu; env != NULL; env = env->next_cpu)
        if (cpu_has_work(env))
            return 1;
    return 0;
}

static int qemu_calculate_timeout(void)
{
    int timeout;

    if (!vm_running)
        timeout = 5000;
    else if (tcg_has_work())
        timeout = 0;
    else if (!use_icount)
        timeout = 5000;
    else {
     /* XXX: use timeout computed from timers */
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
    }

    return timeout;
}

static int vm_can_run(void)
{
    if (powerdown_requested)
        return 0;
    if (reset_requested)
        return 0;
    if (shutdown_requested)
        return 0;
    if (debug_requested)
        return 0;
    return 1;
}

static void main_loop(void)
{
    int r;

#ifdef CONFIG_IOTHREAD
    qemu_system_ready = 1;
    qemu_cond_broadcast(&qemu_system_cond);
#endif

    for (;;) {
        do {
#ifdef CONFIG_PROFILER
            int64_t ti;
#endif
#ifndef CONFIG_IOTHREAD
            tcg_cpu_exec();
#endif
#ifdef CONFIG_PROFILER
            ti = profile_getclock();
#endif
#ifdef CONFIG_IOTHREAD
            main_loop_wait(1000);
#else
            main_loop_wait(qemu_calculate_timeout());
#endif
#ifdef CONFIG_PROFILER
            dev_time += profile_getclock() - ti;
#endif
        } while (vm_can_run());

        if (qemu_debug_requested())
            vm_stop(EXCP_DEBUG);
        if (qemu_shutdown_requested()) {
            if (no_shutdown) {
                vm_stop(0);
                no_shutdown = 0;
            } else
                break;
        }
        if (qemu_reset_requested()) {
            pause_all_vcpus();
            qemu_system_reset();
            resume_all_vcpus();
        }
        if (qemu_powerdown_requested())
            qemu_system_powerdown();
        if ((r = qemu_vmstop_requested()))
            vm_stop(r);
    }
    pause_all_vcpus();
}

static void version(void)
{
    printf("QEMU PC emulator version " QEMU_VERSION QEMU_PKGVERSION ", Copyright (c) 2003-2008 Fabrice Bellard\n");
}

static void help(int exitcode)
{
    version();
    printf("usage: %s [options] [disk_image]\n"
           "\n"
           "'disk_image' is a raw hard image image for IDE hard disk 0\n"
           "\n"
#define DEF(option, opt_arg, opt_enum, opt_help)        \
           opt_help
#define DEFHEADING(text) stringify(text) "\n"
#include "qemu-options.h"
#undef DEF
#undef DEFHEADING
#undef GEN_DOCS
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
#define DEF(option, opt_arg, opt_enum, opt_help)        \
    opt_enum,
#define DEFHEADING(text)
#include "qemu-options.h"
#undef DEF
#undef DEFHEADING
#undef GEN_DOCS
};

typedef struct QEMUOption {
    const char *name;
    int flags;
    int index;
} QEMUOption;

static const QEMUOption qemu_options[] = {
    { "h", 0, QEMU_OPTION_h },
#define DEF(option, opt_arg, opt_enum, opt_help)        \
    { option, opt_arg, opt_enum },
#define DEFHEADING(text)
#include "qemu-options.h"
#undef DEF
#undef DEFHEADING
#undef GEN_DOCS
    { NULL },
};

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

#ifdef CONFIG_SB16
    {
        "sb16",
        "Creative Sound Blaster 16",
        0,
        1,
        { .init_isa = SB16_init }
    },
#endif

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

#ifdef CONFIG_ES1370
    {
        "es1370",
        "ENSONIQ AudioPCI ES1370",
        0,
        0,
        { .init_pci = es1370_init }
    },
#endif

#endif /* HAS_AUDIO_CHOICE */

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

    cirrus_vga_enabled = 0;
    std_vga_enabled = 0;
    vmsvga_enabled = 0;
    xenfb_enabled = 0;
    if (strstart(p, "std", &opts)) {
        std_vga_enabled = 1;
    } else if (strstart(p, "cirrus", &opts)) {
        cirrus_vga_enabled = 1;
    } else if (strstart(p, "vmware", &opts)) {
        vmsvga_enabled = 1;
    } else if (strstart(p, "xenfb", &opts)) {
        xenfb_enabled = 1;
    } else if (!strstart(p, "none", &opts)) {
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

int qemu_uuid_parse(const char *str, uint8_t *uuid)
{
    int ret;

    if(strlen(str) != 36)
        return -1;

    ret = sscanf(str, UUID_FMT, &uuid[0], &uuid[1], &uuid[2], &uuid[3],
            &uuid[4], &uuid[5], &uuid[6], &uuid[7], &uuid[8], &uuid[9],
            &uuid[10], &uuid[11], &uuid[12], &uuid[13], &uuid[14], &uuid[15]);

    if(ret != 16)
        return -1;

#ifdef TARGET_I386
    smbios_add_field(1, offsetof(struct smbios_type_1, uuid), 16, uuid);
#endif

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

int main(int argc, char **argv, char **envp)
{
    const char *gdbstub_dev = NULL;
    uint32_t boot_devices_bitmap = 0;
    int i;
    int snapshot, linux_boot, net_boot;
    const char *initrd_filename;
    const char *kernel_filename, *kernel_cmdline;
    const char *boot_devices = "";
    DisplayState *ds;
    DisplayChangeListener *dcl;
    int cyls, heads, secs, translation;
    const char *net_clients[MAX_NET_CLIENTS];
    int nb_net_clients;
    const char *bt_opts[MAX_BT_CMDLINE];
    int nb_bt_opts;
    int hda_index;
    int optind;
    const char *r, *optarg;
    CharDriverState *monitor_hd = NULL;
    const char *monitor_device;
    const char *serial_devices[MAX_SERIAL_PORTS];
    int serial_device_index;
    const char *parallel_devices[MAX_PARALLEL_PORTS];
    int parallel_device_index;
    const char *virtio_consoles[MAX_VIRTIO_CONSOLES];
    int virtio_console_index;
    const char *loadvm = NULL;
    QEMUMachine *machine;
    const char *cpu_model;
    const char *usb_devices[MAX_USB_CMDLINE];
    int usb_devices_index;
#ifndef _WIN32
    int fds[2];
#endif
    int tb_size;
    const char *pid_file = NULL;
    const char *incoming = NULL;
#ifndef _WIN32
    int fd = 0;
    struct passwd *pwd = NULL;
    const char *chroot_dir = NULL;
    const char *run_as = NULL;
#endif
    CPUState *env;

    qemu_cache_utils_init(envp);

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
    snapshot = 0;
    nographic = 0;
    curses = 0;
    kernel_filename = NULL;
    kernel_cmdline = "";
    cyls = heads = secs = 0;
    translation = BIOS_ATA_TRANSLATION_AUTO;
    monitor_device = "vc:80Cx24C";

    serial_devices[0] = "vc:80Cx24C";
    for(i = 1; i < MAX_SERIAL_PORTS; i++)
        serial_devices[i] = NULL;
    serial_device_index = 0;

    parallel_devices[0] = "vc:80Cx24C";
    for(i = 1; i < MAX_PARALLEL_PORTS; i++)
        parallel_devices[i] = NULL;
    parallel_device_index = 0;

    for(i = 0; i < MAX_VIRTIO_CONSOLES; i++)
        virtio_consoles[i] = NULL;
    virtio_console_index = 0;

    for (i = 0; i < MAX_NODES; i++) {
        node_mem[i] = 0;
        node_cpumask[i] = 0;
    }

    usb_devices_index = 0;

    nb_net_clients = 0;
    nb_bt_opts = 0;
    nb_drives = 0;
    nb_drives_opt = 0;
    nb_numa_nodes = 0;
    hda_index = -1;

    nb_nics = 0;

    tb_size = 0;
    autostart= 1;

    register_watchdogs();

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
            case QEMU_OPTION_numa:
                if (nb_numa_nodes >= MAX_NODES) {
                    fprintf(stderr, "qemu: too many NUMA nodes\n");
                    exit(1);
                }
                numa_add(optarg);
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
                net_slirp_redir(NULL, optarg);
                break;
#endif
            case QEMU_OPTION_bt:
                if (nb_bt_opts >= MAX_BT_CMDLINE) {
                    fprintf(stderr, "qemu: too many bluetooth options\n");
                    exit(1);
                }
                bt_opts[nb_bt_opts++] = optarg;
                break;
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
            case QEMU_OPTION_version:
                version();
                exit(0);
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
#ifndef CONFIG_KQEMU
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
            case QEMU_OPTION_s:
                gdbstub_dev = "tcp::" DEFAULT_GDBSTUB_PORT;
                break;
            case QEMU_OPTION_gdb:
                gdbstub_dev = optarg;
                break;
            case QEMU_OPTION_L:
                bios_dir = optarg;
                break;
            case QEMU_OPTION_bios:
                bios_name = optarg;
                break;
            case QEMU_OPTION_singlestep:
                singlestep = 1;
                break;
            case QEMU_OPTION_S:
                autostart = 0;
                break;
#ifndef _WIN32
	    case QEMU_OPTION_k:
		keyboard_layout = optarg;
		break;
#endif
            case QEMU_OPTION_localtime:
                rtc_utc = 0;
                break;
            case QEMU_OPTION_vga:
                select_vgahw (optarg);
                break;
#if defined(TARGET_PPC) || defined(TARGET_SPARC)
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
#endif
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
            case QEMU_OPTION_watchdog:
                i = select_watchdog(optarg);
                if (i > 0)
                    exit (i == 1 ? 1 : 0);
                break;
            case QEMU_OPTION_watchdog_action:
                if (select_watchdog_action(optarg) == -1) {
                    fprintf(stderr, "Unknown -watchdog-action parameter\n");
                    exit(1);
                }
                break;
            case QEMU_OPTION_virtiocon:
                if (virtio_console_index >= MAX_VIRTIO_CONSOLES) {
                    fprintf(stderr, "qemu: too many virtio consoles\n");
                    exit(1);
                }
                virtio_consoles[virtio_console_index] = optarg;
                virtio_console_index++;
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
            case QEMU_OPTION_sdl:
                sdl = 1;
                break;
#endif
            case QEMU_OPTION_pidfile:
                pid_file = optarg;
                break;
#ifdef TARGET_I386
            case QEMU_OPTION_win2k_hack:
                win2k_install_hack = 1;
                break;
            case QEMU_OPTION_rtc_td_hack:
                rtc_td_hack = 1;
                break;
            case QEMU_OPTION_acpitable:
                if(acpi_table_add(optarg) < 0) {
                    fprintf(stderr, "Wrong acpi table provided\n");
                    exit(1);
                }
                break;
            case QEMU_OPTION_smbios:
                if(smbios_entry_add(optarg) < 0) {
                    fprintf(stderr, "Wrong smbios provided\n");
                    exit(1);
                }
                break;
#endif
#ifdef CONFIG_KQEMU
            case QEMU_OPTION_no_kqemu:
                kqemu_allowed = 0;
                break;
            case QEMU_OPTION_kernel_kqemu:
                kqemu_allowed = 2;
                break;
#endif
#ifdef CONFIG_KVM
            case QEMU_OPTION_enable_kvm:
                kvm_allowed = 1;
#ifdef CONFIG_KQEMU
                kqemu_allowed = 0;
#endif
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
#ifdef TARGET_I386
            case QEMU_OPTION_no_acpi:
                acpi_enabled = 0;
                break;
            case QEMU_OPTION_no_hpet:
                no_hpet = 1;
                break;
#endif
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
#ifndef _WIN32
	    case QEMU_OPTION_daemonize:
		daemonize = 1;
		break;
#endif
	    case QEMU_OPTION_option_rom:
		if (nb_option_roms >= MAX_OPTION_ROMS) {
		    fprintf(stderr, "Too many option ROMs\n");
		    exit(1);
		}
		option_rom[nb_option_roms] = optarg;
		nb_option_roms++;
		break;
#if defined(TARGET_ARM) || defined(TARGET_M68K)
            case QEMU_OPTION_semihosting:
                semihosting_enabled = 1;
                break;
#endif
            case QEMU_OPTION_name:
                qemu_name = optarg;
                break;
#if defined(TARGET_SPARC) || defined(TARGET_PPC)
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
            case QEMU_OPTION_incoming:
                incoming = optarg;
                break;
#ifndef _WIN32
            case QEMU_OPTION_chroot:
                chroot_dir = optarg;
                break;
            case QEMU_OPTION_runas:
                run_as = optarg;
                break;
#endif
#ifdef CONFIG_XEN
            case QEMU_OPTION_xen_domid:
                xen_domid = atoi(optarg);
                break;
            case QEMU_OPTION_xen_create:
                xen_mode = XEN_CREATE;
                break;
            case QEMU_OPTION_xen_attach:
                xen_mode = XEN_ATTACH;
                break;
#endif
            }
        }
    }

#if defined(CONFIG_KVM) && defined(CONFIG_KQEMU)
    if (kvm_allowed && kqemu_allowed) {
        fprintf(stderr,
                "You can not enable both KVM and kqemu at the same time\n");
        exit(1);
    }
#endif

    machine->max_cpus = machine->max_cpus ?: 1; /* Default to UP */
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

    if (pid_file && qemu_create_pidfile(pid_file) != 0) {
        if (daemonize) {
            uint8_t status = 1;
            write(fds[1], &status, 1);
        } else
            fprintf(stderr, "Could not acquire pid file\n");
        exit(1);
    }
#endif

#ifdef CONFIG_KQEMU
    if (smp_cpus > 1)
        kqemu_allowed = 0;
#endif
    if (qemu_init_main_loop()) {
        fprintf(stderr, "qemu_init_main_loop failed\n");
        exit(1);
    }
    linux_boot = (kernel_filename != NULL);
    net_boot = (boot_devices_bitmap >> ('n' - 'a')) & 0xF;

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
    if (init_timer_alarm() < 0) {
        fprintf(stderr, "could not initialize alarm timer\n");
        exit(1);
    }
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
    net_client_check();

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

    /* init the bluetooth world */
    for (i = 0; i < nb_bt_opts; i++)
        if (bt_parse(bt_opts[i]))
            exit(1);

    /* init the memory */
    if (ram_size == 0)
        ram_size = DEFAULT_RAM_SIZE * 1024 * 1024;

#ifdef CONFIG_KQEMU
    /* FIXME: This is a nasty hack because kqemu can't cope with dynamic
       guest ram allocation.  It needs to go away.  */
    if (kqemu_allowed) {
        kqemu_phys_ram_size = ram_size + VGA_RAM_SIZE + 4 * 1024 * 1024;
        kqemu_phys_ram_base = qemu_vmalloc(kqemu_phys_ram_size);
        if (!kqemu_phys_ram_base) {
            fprintf(stderr, "Could not allocate physical memory\n");
            exit(1);
        }
    }
#endif

    /* init the dynamic translator */
    cpu_exec_init_all(tb_size * 1024 * 1024);

    bdrv_init();
    dma_helper_init();

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

    if (nb_numa_nodes > 0) {
        int i;

        if (nb_numa_nodes > smp_cpus) {
            nb_numa_nodes = smp_cpus;
        }

        /* If no memory size if given for any node, assume the default case
         * and distribute the available memory equally across all nodes
         */
        for (i = 0; i < nb_numa_nodes; i++) {
            if (node_mem[i] != 0)
                break;
        }
        if (i == nb_numa_nodes) {
            uint64_t usedmem = 0;

            /* On Linux, the each node's border has to be 8MB aligned,
             * the final node gets the rest.
             */
            for (i = 0; i < nb_numa_nodes - 1; i++) {
                node_mem[i] = (ram_size / nb_numa_nodes) & ~((1 << 23UL) - 1);
                usedmem += node_mem[i];
            }
            node_mem[i] = ram_size - usedmem;
        }

        for (i = 0; i < nb_numa_nodes; i++) {
            if (node_cpumask[i] != 0)
                break;
        }
        /* assigning the VCPUs round-robin is easier to implement, guest OSes
         * must cope with this anyway, because there are BIOSes out there in
         * real machines which also use this scheme.
         */
        if (i == nb_numa_nodes) {
            for (i = 0; i < smp_cpus; i++) {
                node_cpumask[i % nb_numa_nodes] |= 1 << i;
            }
        }
    }

    if (kvm_enabled()) {
        int ret;

        ret = kvm_init(smp_cpus);
        if (ret < 0) {
            fprintf(stderr, "failed to initialize KVM\n");
            exit(1);
        }
    }

    if (monitor_device) {
        monitor_hd = qemu_chr_open("monitor", monitor_device, NULL);
        if (!monitor_hd) {
            fprintf(stderr, "qemu: could not open monitor device '%s'\n", monitor_device);
            exit(1);
        }
    }

    for(i = 0; i < MAX_SERIAL_PORTS; i++) {
        const char *devname = serial_devices[i];
        if (devname && strcmp(devname, "none")) {
            char label[32];
            snprintf(label, sizeof(label), "serial%d", i);
            serial_hds[i] = qemu_chr_open(label, devname, NULL);
            if (!serial_hds[i]) {
                fprintf(stderr, "qemu: could not open serial device '%s'\n",
                        devname);
                exit(1);
            }
        }
    }

    for(i = 0; i < MAX_PARALLEL_PORTS; i++) {
        const char *devname = parallel_devices[i];
        if (devname && strcmp(devname, "none")) {
            char label[32];
            snprintf(label, sizeof(label), "parallel%d", i);
            parallel_hds[i] = qemu_chr_open(label, devname, NULL);
            if (!parallel_hds[i]) {
                fprintf(stderr, "qemu: could not open parallel device '%s'\n",
                        devname);
                exit(1);
            }
        }
    }

    for(i = 0; i < MAX_VIRTIO_CONSOLES; i++) {
        const char *devname = virtio_consoles[i];
        if (devname && strcmp(devname, "none")) {
            char label[32];
            snprintf(label, sizeof(label), "virtcon%d", i);
            virtcon_hds[i] = qemu_chr_open(label, devname, NULL);
            if (!virtcon_hds[i]) {
                fprintf(stderr, "qemu: could not open virtio console '%s'\n",
                        devname);
                exit(1);
            }
        }
    }

    machine->init(ram_size, vga_ram_size, boot_devices,
                  kernel_filename, kernel_cmdline, initrd_filename, cpu_model);


    for (env = first_cpu; env != NULL; env = env->next_cpu) {
        for (i = 0; i < nb_numa_nodes; i++) {
            if (node_cpumask[i] & (1 << env->cpu_index)) {
                env->numa_node = i;
            }
        }
    }

    current_machine = machine;

    /* Set KVM's vcpu state to qemu's initial CPUState. */
    if (kvm_enabled()) {
        int ret;

        ret = kvm_sync_vcpus();
        if (ret < 0) {
            fprintf(stderr, "failed to initialize vcpus\n");
            exit(1);
        }
    }

    /* init USB devices */
    if (usb_enabled) {
        for(i = 0; i < usb_devices_index; i++) {
            if (usb_device_add(usb_devices[i], 0) < 0) {
                fprintf(stderr, "Warning: could not add USB device %s\n",
                        usb_devices[i]);
            }
        }
    }

    if (!display_state)
        dumb_display_init();
    /* just use the first displaystate for the moment */
    ds = display_state;
    /* terminal init */
    if (nographic) {
        if (curses) {
            fprintf(stderr, "fatal: -nographic can't be used with -curses\n");
            exit(1);
        }
    } else { 
#if defined(CONFIG_CURSES)
            if (curses) {
                /* At the moment curses cannot be used with other displays */
                curses_display_init(ds, full_screen);
            } else
#endif
            {
                if (vnc_display != NULL) {
                    vnc_display_init(ds);
                    if (vnc_display_open(ds, vnc_display) < 0)
                        exit(1);
                }
#if defined(CONFIG_SDL)
                if (sdl || !vnc_display)
                    sdl_display_init(ds, full_screen, no_frame);
#elif defined(CONFIG_COCOA)
                if (sdl || !vnc_display)
                    cocoa_display_init(ds, full_screen);
#endif
            }
    }
    dpy_resize(ds);

    dcl = ds->listeners;
    while (dcl != NULL) {
        if (dcl->dpy_refresh != NULL) {
            ds->gui_timer = qemu_new_timer(rt_clock, gui_update, ds);
            qemu_mod_timer(ds->gui_timer, qemu_get_clock(rt_clock));
        }
        dcl = dcl->next;
    }

    if (nographic || (vnc_display && !sdl)) {
        nographic_timer = qemu_new_timer(rt_clock, nographic_update, NULL);
        qemu_mod_timer(nographic_timer, qemu_get_clock(rt_clock));
    }

    text_consoles_set_display(display_state);
    qemu_chr_initial_reset();

    if (monitor_device && monitor_hd)
        monitor_init(monitor_hd, MONITOR_USE_READLINE | MONITOR_IS_DEFAULT);

    for(i = 0; i < MAX_SERIAL_PORTS; i++) {
        const char *devname = serial_devices[i];
        if (devname && strcmp(devname, "none")) {
            char label[32];
            snprintf(label, sizeof(label), "serial%d", i);
            if (strstart(devname, "vc", 0))
                qemu_chr_printf(serial_hds[i], "serial%d console\r\n", i);
        }
    }

    for(i = 0; i < MAX_PARALLEL_PORTS; i++) {
        const char *devname = parallel_devices[i];
        if (devname && strcmp(devname, "none")) {
            char label[32];
            snprintf(label, sizeof(label), "parallel%d", i);
            if (strstart(devname, "vc", 0))
                qemu_chr_printf(parallel_hds[i], "parallel%d console\r\n", i);
        }
    }

    for(i = 0; i < MAX_VIRTIO_CONSOLES; i++) {
        const char *devname = virtio_consoles[i];
        if (virtcon_hds[i] && devname) {
            char label[32];
            snprintf(label, sizeof(label), "virtcon%d", i);
            if (strstart(devname, "vc", 0))
                qemu_chr_printf(virtcon_hds[i], "virtio console%d\r\n", i);
        }
    }

    if (gdbstub_dev && gdbserver_start(gdbstub_dev) < 0) {
        fprintf(stderr, "qemu: could not open gdbserver on device '%s'\n",
                gdbstub_dev);
        exit(1);
    }

    if (loadvm)
        do_loadvm(cur_mon, loadvm);

    if (incoming) {
        autostart = 0; /* fixme how to deal with -daemonize */
        qemu_start_incoming_migration(incoming);
    }

    if (autostart)
        vm_start();

#ifndef _WIN32
    if (daemonize) {
	uint8_t status = 0;
	ssize_t len;

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
    }

    if (run_as) {
        pwd = getpwnam(run_as);
        if (!pwd) {
            fprintf(stderr, "User \"%s\" doesn't exist\n", run_as);
            exit(1);
        }
    }

    if (chroot_dir) {
        if (chroot(chroot_dir) < 0) {
            fprintf(stderr, "chroot failed\n");
            exit(1);
        }
        chdir("/");
    }

    if (run_as) {
        if (setgid(pwd->pw_gid) < 0) {
            fprintf(stderr, "Failed to setgid(%d)\n", pwd->pw_gid);
            exit(1);
        }
        if (setuid(pwd->pw_uid) < 0) {
            fprintf(stderr, "Failed to setuid(%d)\n", pwd->pw_uid);
            exit(1);
        }
        if (setuid(0) != -1) {
            fprintf(stderr, "Dropping privileges failed\n");
            exit(1);
        }
    }

    if (daemonize) {
        dup2(fd, 0);
        dup2(fd, 1);
        dup2(fd, 2);

        close(fd);
    }
#endif

    main_loop();
    quit_timers();
    net_cleanup();

    return 0;
}
