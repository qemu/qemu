#ifndef QEMU_CONFIG_H
#define QEMU_CONFIG_H

#include <stdio.h>
#include "qemu/option.h"
#include "qapi/error.h"
#include "qapi/qmp/qdict.h"

QemuOptsList *qemu_find_opts(const char *group);
QemuOptsList *qemu_find_opts_err(const char *group, Error **errp);
void qemu_add_opts(QemuOptsList *list);
void qemu_add_drive_opts(QemuOptsList *list);
int qemu_set_option(const char *str);
int qemu_global_option(const char *str);
void qemu_add_globals(void);

void qemu_config_write(FILE *fp);
int qemu_config_parse(FILE *fp, QemuOptsList **lists, const char *fname);

int qemu_read_config_file(const char *filename);

/* Parse QDict options as a replacement for a config file (allowing multiple
   enumerated (0..(n-1)) configuration "sections") */
void qemu_config_parse_qdict(QDict *options, QemuOptsList **lists,
                             Error **errp);

/* Read default QEMU config files
 */
int qemu_read_default_config_files(bool userconfig);

#endif /* QEMU_CONFIG_H */
