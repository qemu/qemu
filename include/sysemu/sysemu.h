#ifndef SYSEMU_H
#define SYSEMU_H
/* Misc. things related to the system emulator.  */

#include "qemu/timer.h"
#include "qemu/notify.h"
#include "qemu/uuid.h"

/* vl.c */

extern int only_migratable;
extern const char *qemu_name;
extern QemuUUID qemu_uuid;
extern bool qemu_uuid_set;

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

extern int graphic_width;
extern int graphic_height;
extern int graphic_depth;
extern int display_opengl;
extern const char *keyboard_layout;
extern int win2k_install_hack;
extern int alt_grab;
extern int ctrl_grab;
extern int graphic_rotate;
extern int no_shutdown;
extern int old_param;
extern int boot_menu;
extern bool boot_strict;
extern uint8_t *boot_splash_filedata;
extern bool enable_mlock;
extern bool enable_cpu_pm;
extern QEMUClockType rtc_clock;

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
                                   DeviceState *dev);
void restore_boot_order(void *opaque);
void validate_bootdevices(const char *devices, Error **errp);
void add_boot_device_lchs(DeviceState *dev, const char *suffix,
                          uint32_t lcyls, uint32_t lheads, uint32_t lsecs);
void del_boot_device_lchs(DeviceState *dev, const char *suffix);
char *get_boot_devices_lchs_list(size_t *size);

/* handler to set the boot_device order for a specific type of MachineClass */
typedef void QEMUBootSetHandler(void *opaque, const char *boot_order,
                                Error **errp);
void qemu_register_boot_set(QEMUBootSetHandler *func, void *opaque);
void qemu_boot_set(const char *boot_order, Error **errp);

QemuOpts *qemu_get_machine_opts(void);

bool defaults_enabled(void);

void qemu_init(int argc, char **argv, char **envp);
void qemu_main_loop(void);
void qemu_cleanup(void);

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
extern QemuOptsList qemu_semihosting_config_opts;

#endif
