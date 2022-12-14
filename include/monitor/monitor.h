#ifndef MONITOR_H
#define MONITOR_H

#include "block/block.h"
#include "qapi/qapi-types-misc.h"
#include "qemu/readline.h"
#include "exec/hwaddr.h"

typedef struct MonitorHMP MonitorHMP;
typedef struct MonitorOptions MonitorOptions;

#define QMP_REQ_QUEUE_LEN_MAX 8

extern QemuOptsList qemu_mon_opts;

Monitor *monitor_cur(void);
Monitor *monitor_set_cur(Coroutine *co, Monitor *mon);
bool monitor_cur_is_qmp(void);

void monitor_init_globals(void);
void monitor_init_globals_core(void);
void monitor_init_qmp(Chardev *chr, bool pretty, Error **errp);
void monitor_init_hmp(Chardev *chr, bool use_readline, Error **errp);
int monitor_init(MonitorOptions *opts, bool allow_hmp, Error **errp);
int monitor_init_opts(QemuOpts *opts, Error **errp);
void monitor_cleanup(void);

int monitor_suspend(Monitor *mon);
void monitor_resume(Monitor *mon);

int monitor_get_fd(Monitor *mon, const char *fdname, Error **errp);
int monitor_fd_param(Monitor *mon, const char *fdname, Error **errp);

int monitor_puts(Monitor *mon, const char *str);
int monitor_vprintf(Monitor *mon, const char *fmt, va_list ap)
    G_GNUC_PRINTF(2, 0);
int monitor_printf(Monitor *mon, const char *fmt, ...) G_GNUC_PRINTF(2, 3);
void monitor_flush(Monitor *mon);
int monitor_set_cpu(Monitor *mon, int cpu_index);
int monitor_get_cpu_index(Monitor *mon);

void *gpa2hva(MemoryRegion **p_mr, hwaddr addr, uint64_t size, Error **errp);

void monitor_read_command(MonitorHMP *mon, int show_prompt);
int monitor_read_password(MonitorHMP *mon, ReadLineFunc *readline_func,
                          void *opaque);

AddfdInfo *monitor_fdset_add_fd(int fd, bool has_fdset_id, int64_t fdset_id,
                                const char *opaque, Error **errp);
int monitor_fdset_dup_fd_add(int64_t fdset_id, int flags);
void monitor_fdset_dup_fd_remove(int dup_fd);
int64_t monitor_fdset_dup_fd_find(int dup_fd);

void monitor_register_hmp(const char *name, bool info,
                          void (*cmd)(Monitor *mon, const QDict *qdict));
void monitor_register_hmp_info_hrt(const char *name,
                                   HumanReadableText *(*handler)(Error **errp));

int error_vprintf_unless_qmp(const char *fmt, va_list ap) G_GNUC_PRINTF(1, 0);
int error_printf_unless_qmp(const char *fmt, ...) G_GNUC_PRINTF(1, 2);

#endif /* MONITOR_H */
