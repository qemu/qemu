#ifndef QEMU_CONFIG_H
#define QEMU_CONFIG_H

extern QemuOptsList qemu_drive_opts;
extern QemuOptsList qemu_chardev_opts;
extern QemuOptsList qemu_device_opts;

int qemu_set_option(const char *str);

#endif /* QEMU_CONFIG_H */
