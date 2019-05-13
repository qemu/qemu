#ifndef SYSEMU_H
#define SYSEMU_H
/* Misc. things related to the system emulator.  */

#include "qapi/qapi-types-run-state.h"
#include "qemu/queue.h"
#include "qemu/timer.h"
#include "qemu/notify.h"
#include "qemu/main-loop.h"
#include "qemu/bitmap.h"
#include "qemu/uuid.h"
#include "qom/object.h"

/* vl.c */

extern const char *bios_name;
extern int only_migratable;
extern const char *qemu_name;
extern QemuUUID qemu_uuid;
extern bool qemu_uuid_set;

bool runstate_check(RunState state);
void runstate_set(RunState new_state);
int runstate_is_running(void);
bool runstate_needs_reset(void);
bool runstate_store(char *str, size_t size);
typedef struct vm_change_state_entry VMChangeStateEntry;
typedef void VMChangeStateHandler(void *opaque, int running, RunState state);

VMChangeStateEntry *qemu_add_vm_change_state_handler(VMChangeStateHandler *cb,
                                                     void *opaque);
void qemu_del_vm_change_state_handler(VMChangeStateEntry *e);
void vm_state_notify(int running, RunState state);

static inline bool shutdown_caused_by_guest(ShutdownCause cause)
{
    return cause >= SHUTDOWN_CAUSE_GUEST_SHUTDOWN;
}

void vm_start(void);
int vm_prepare_start(void);
int vm_stop(RunState state);
int vm_stop_force_state(RunState state);
int vm_shutdown(void);

typedef enum WakeupReason {
    /* Always keep QEMU_WAKEUP_REASON_NONE = 0 */
    QEMU_WAKEUP_REASON_NONE = 0,
    QEMU_WAKEUP_REASON_RTC,
    QEMU_WAKEUP_REASON_PMTIMER,
    QEMU_WAKEUP_REASON_OTHER,
} WakeupReason;

void qemu_exit_preconfig_request(void);
void qemu_system_reset_request(ShutdownCause reason);
void qemu_system_suspend_request(void);
void qemu_register_suspend_notifier(Notifier *notifier);
bool qemu_wakeup_suspend_enabled(void);
void qemu_system_wakeup_request(WakeupReason reason, Error **errp);
void qemu_system_wakeup_enable(WakeupReason reason, bool enabled);
void qemu_register_wakeup_notifier(Notifier *notifier);
void qemu_register_wakeup_support(void);
void qemu_system_shutdown_request(ShutdownCause reason);
void qemu_system_powerdown_request(void);
void qemu_register_powerdown_notifier(Notifier *notifier);
void qemu_register_shutdown_notifier(Notifier *notifier);
void qemu_system_debug_request(void);
void qemu_system_vmstop_request(RunState reason);
void qemu_system_vmstop_request_prepare(void);
bool qemu_vmstop_requested(RunState *r);
ShutdownCause qemu_shutdown_requested_get(void);
ShutdownCause qemu_reset_requested_get(void);
void qemu_system_killed(int signal, pid_t pid);
void qemu_system_reset(ShutdownCause reason);
void qemu_system_guest_panicked(GuestPanicInformation *info);

void qemu_add_exit_notifier(Notifier *notify);
void qemu_remove_exit_notifier(Notifier *notify);

extern bool machine_init_done;

void qemu_add_machine_init_done_notifier(Notifier *notify);
void qemu_remove_machine_init_done_notifier(Notifier *notify);

extern int autostart;

typedef enum {
    VGA_NONE, VGA_STD, VGA_CIRRUS, VGA_VMWARE, VGA_XENFB, VGA_QXL,
    VGA_TCX, VGA_CG3, VGA_DEVICE, VGA_VIRTIO,
    VGA_TYPE_MAX,
} VGAInterfaceType;

extern int vga_interface_type;
#define xenfb_enabled (vga_interface_type == VGA_XENFB)

extern int graphic_width;
extern int graphic_height;
extern int graphic_depth;
extern int display_opengl;
extern const char *keyboard_layout;
extern int win2k_install_hack;
extern int alt_grab;
extern int ctrl_grab;
extern int smp_cpus;
extern unsigned int max_cpus;
extern int cursor_hide;
extern int graphic_rotate;
extern int no_quit;
extern int no_shutdown;
extern int old_param;
extern int boot_menu;
extern bool boot_strict;
extern uint8_t *boot_splash_filedata;
extern bool enable_mlock;
extern bool enable_cpu_pm;
extern QEMUClockType rtc_clock;
extern const char *mem_path;
extern int mem_prealloc;

#define MAX_NODES 128
#define NUMA_NODE_UNASSIGNED MAX_NODES
#define NUMA_DISTANCE_MIN         10
#define NUMA_DISTANCE_DEFAULT     20
#define NUMA_DISTANCE_MAX         254
#define NUMA_DISTANCE_UNREACHABLE 255

#define MAX_OPTION_ROMS 16
typedef struct QEMUOptionRom {
    const char *name;
    int32_t bootindex;
} QEMUOptionRom;
extern QEMUOptionRom option_rom[MAX_OPTION_ROMS];
extern int nb_option_roms;

#define MAX_PROM_ENVS 128
extern const char *prom_envs[MAX_PROM_ENVS];
extern unsigned int nb_prom_envs;

/* generic hotplug */
void hmp_drive_add(Monitor *mon, const QDict *qdict);

/* pcie aer error injection */
void hmp_pcie_aer_inject_error(Monitor *mon, const QDict *qdict);

/* serial ports */

/* Return the Chardev for serial port i, or NULL if none */
Chardev *serial_hd(int i);
/* return the number of serial ports defined by the user. serial_hd(i)
 * will always return NULL for any i which is greater than or equal to this.
 */
int serial_max_hds(void);

/* parallel ports */

#define MAX_PARALLEL_PORTS 3

extern Chardev *parallel_hds[MAX_PARALLEL_PORTS];

void hmp_info_usb(Monitor *mon, const QDict *qdict);

void add_boot_device_path(int32_t bootindex, DeviceState *dev,
                          const char *suffix);
char *get_boot_devices_list(size_t *size);

DeviceState *get_boot_device(uint32_t position);
void check_boot_index(int32_t bootindex, Error **errp);
void del_boot_device_path(DeviceState *dev, const char *suffix);
void device_add_bootindex_property(Object *obj, int32_t *bootindex,
                                   const char *name, const char *suffix,
                                   DeviceState *dev, Error **errp);
void restore_boot_order(void *opaque);
void validate_bootdevices(const char *devices, Error **errp);

/* handler to set the boot_device order for a specific type of MachineClass */
typedef void QEMUBootSetHandler(void *opaque, const char *boot_order,
                                Error **errp);
void qemu_register_boot_set(QEMUBootSetHandler *func, void *opaque);
void qemu_boot_set(const char *boot_order, Error **errp);

QemuOpts *qemu_get_machine_opts(void);

bool defaults_enabled(void);

extern QemuOptsList qemu_legacy_drive_opts;
extern QemuOptsList qemu_common_drive_opts;
extern QemuOptsList qemu_drive_opts;
extern QemuOptsList bdrv_runtime_opts;
extern QemuOptsList qemu_chardev_opts;
extern QemuOptsList qemu_device_opts;
extern QemuOptsList qemu_netdev_opts;
extern QemuOptsList qemu_nic_opts;
extern QemuOptsList qemu_net_opts;
extern QemuOptsList qemu_global_opts;
extern QemuOptsList qemu_mon_opts;
extern QemuOptsList qemu_semihosting_config_opts;

#endif
