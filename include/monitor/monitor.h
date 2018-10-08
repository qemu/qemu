#ifndef MONITOR_H
#define MONITOR_H

#include "qemu-common.h"
#include "block/block.h"
#include "qapi/qapi-types-misc.h"
#include "qemu/readline.h"

extern __thread Monitor *cur_mon;

/* flags for monitor_init */
/* 0x01 unused */
#define MONITOR_USE_READLINE  0x02
#define MONITOR_USE_CONTROL   0x04
#define MONITOR_USE_PRETTY    0x08
#define MONITOR_USE_OOB       0x10

bool monitor_cur_is_qmp(void);

void monitor_init_globals(void);
void monitor_init(Chardev *chr, int flags);
void monitor_cleanup(void);

int monitor_suspend(Monitor *mon);
void monitor_resume(Monitor *mon);

int monitor_get_fd(Monitor *mon, const char *fdname, Error **errp);
int monitor_fd_param(Monitor *mon, const char *fdname, Error **errp);

void monitor_vprintf(Monitor *mon, const char *fmt, va_list ap)
    GCC_FMT_ATTR(2, 0);
void monitor_printf(Monitor *mon, const char *fmt, ...) GCC_FMT_ATTR(2, 3);
int monitor_fprintf(FILE *stream, const char *fmt, ...) GCC_FMT_ATTR(2, 3);
void monitor_flush(Monitor *mon);
int monitor_set_cpu(int cpu_index);
int monitor_get_cpu_index(void);

void monitor_read_command(Monitor *mon, int show_prompt);
int monitor_read_password(Monitor *mon, ReadLineFunc *readline_func,
                          void *opaque);

AddfdInfo *monitor_fdset_add_fd(int fd, bool has_fdset_id, int64_t fdset_id,
                                bool has_opaque, const char *opaque,
                                Error **errp);
int monitor_fdset_get_fd(int64_t fdset_id, int flags);
int monitor_fdset_dup_fd_add(int64_t fdset_id, int dup_fd);
void monitor_fdset_dup_fd_remove(int dup_fd);
int monitor_fdset_dup_fd_find(int dup_fd);

void monitor_vfprintf(FILE *stream,
                      const char *fmt, va_list ap) GCC_FMT_ATTR(2, 0);

#endif /* MONITOR_H */
