#ifndef QEMU_CONFIG_H
#define QEMU_CONFIG_H

extern QemuOptsList qemu_drive_opts;
extern QemuOptsList qemu_chardev_opts;
#ifdef CONFIG_LINUX
extern QemuOptsList qemu_fsdev_opts;
extern QemuOptsList qemu_virtfs_opts;
#endif
extern QemuOptsList qemu_device_opts;
extern QemuOptsList qemu_netdev_opts;
extern QemuOptsList qemu_net_opts;
extern QemuOptsList qemu_rtc_opts;
extern QemuOptsList qemu_global_opts;
extern QemuOptsList qemu_mon_opts;
extern QemuOptsList qemu_cpudef_opts;

QemuOptsList *qemu_find_opts(const char *group);
int qemu_set_option(const char *str);
int qemu_global_option(const char *str);
void qemu_add_globals(void);

void qemu_config_write(FILE *fp);
int qemu_config_parse(FILE *fp, QemuOptsList **lists, const char *fname);

int qemu_read_config_file(const char *filename);

#endif /* QEMU_CONFIG_H */
