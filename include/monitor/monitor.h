#ifndef MONITOR_H
#define MONITOR_H

#include "qapi/qapi-types-misc.h"
#include "qemu/coroutine-core.h"
#include "qemu/readline.h"
#include "exec/hwaddr.h"
#include "qom/object.h"

#define TYPE_MONITOR "monitor"
OBJECT_DECLARE_TYPE(Monitor, MonitorClass, MONITOR);

#define TYPE_MONITOR_HMP "monitor-hmp"
OBJECT_DECLARE_TYPE(MonitorHMP, MonitorHMPClass, MONITOR_HMP);

#define TYPE_MONITOR_QMP "monitor-qmp"
OBJECT_DECLARE_TYPE(MonitorQMP, MonitorQMPClass, MONITOR_QMP);

typedef struct MonitorOptions MonitorOptions;

#define QMP_REQ_QUEUE_LEN_MAX 8

extern QemuOptsList qemu_mon_opts;

Monitor *monitor_cur(void);
Monitor *monitor_set_cur(Coroutine *co, Monitor *mon);

void monitor_init_globals(void);
void monitor_init_globals_core(void);
char *monitor_compat_id(void);
void monitor_new_qmp(const char *id, const char *chardev_id,
                     bool pretty, Error **errp);
void monitor_new_hmp(const char *id, const char *chardev_id,
                     bool use_readline, Error **errp);
int monitor_new(MonitorOptions *opts, bool allow_hmp, Error **errp);
int monitor_new_opts(QemuOpts *opts, Error **errp);
void monitor_cleanup(void);

void monitor_suspend(Monitor *mon);
void monitor_resume(Monitor *mon);

int monitor_get_fd(Monitor *mon, const char *fdname, Error **errp);
int monitor_fd_param(Monitor *mon, const char *fdname, Error **errp);

int monitor_puts(Monitor *mon, const char *str);
int monitor_vprintf(Monitor *mon, const char *fmt, va_list ap)
    G_GNUC_PRINTF(2, 0);
int monitor_printf(Monitor *mon, const char *fmt, ...) G_GNUC_PRINTF(2, 3);
void monitor_printc(Monitor *mon, int ch);
void monitor_flush(Monitor *mon);
int monitor_get_cpu_index(Monitor *mon);

int monitor_puts_locked(Monitor *mon, const char *str);
void monitor_flush_locked(Monitor *mon);

void monitor_read_command(MonitorHMP *hmp, int show_prompt);
int monitor_read_password(MonitorHMP *hmp, ReadLineFunc *readline_func,
                          void *opaque);

AddfdInfo *monitor_fdset_add_fd(int fd, bool has_fdset_id, int64_t fdset_id,
                                const char *opaque, Error **errp);
int monitor_fdset_dup_fd_add(int64_t fdset_id, int flags, Error **errp);
void monitor_fdset_dup_fd_remove(int dup_fd);

void monitor_register_hmp(const char *name, bool info,
                          void (*cmd)(Monitor *mon, const QDict *qdict));
void monitor_register_hmp_info_hrt(const char *name,
                                   HumanReadableText *(*handler)(Error **errp));

#endif /* MONITOR_H */
