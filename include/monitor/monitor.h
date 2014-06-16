#ifndef MONITOR_H
#define MONITOR_H

#include "qemu-common.h"
#include "qapi/qmp/qerror.h"
#include "qapi/qmp/qdict.h"
#include "block/block.h"
#include "qemu/readline.h"

extern Monitor *cur_mon;
extern Monitor *default_mon;

/* flags for monitor_init */
#define MONITOR_IS_DEFAULT    0x01
#define MONITOR_USE_READLINE  0x02
#define MONITOR_USE_CONTROL   0x04
#define MONITOR_USE_PRETTY    0x08

/* flags for monitor commands */
#define MONITOR_CMD_ASYNC       0x0001

/* QMP events */
typedef enum MonitorEvent {
    QEVENT_SHUTDOWN,
    QEVENT_RESET,
    QEVENT_POWERDOWN,
    QEVENT_STOP,
    QEVENT_RESUME,
    QEVENT_VNC_CONNECTED,
    QEVENT_VNC_INITIALIZED,
    QEVENT_VNC_DISCONNECTED,
    QEVENT_BLOCK_IO_ERROR,
    QEVENT_RTC_CHANGE,
    QEVENT_WATCHDOG,
    QEVENT_SPICE_CONNECTED,
    QEVENT_SPICE_INITIALIZED,
    QEVENT_SPICE_DISCONNECTED,
    QEVENT_BLOCK_JOB_COMPLETED,
    QEVENT_BLOCK_JOB_CANCELLED,
    QEVENT_BLOCK_JOB_ERROR,
    QEVENT_BLOCK_JOB_READY,
    QEVENT_DEVICE_DELETED,
    QEVENT_DEVICE_TRAY_MOVED,
    QEVENT_NIC_RX_FILTER_CHANGED,
    QEVENT_SUSPEND,
    QEVENT_SUSPEND_DISK,
    QEVENT_WAKEUP,
    QEVENT_BALLOON_CHANGE,
    QEVENT_SPICE_MIGRATE_COMPLETED,
    QEVENT_GUEST_PANICKED,
    QEVENT_BLOCK_IMAGE_CORRUPTED,
    QEVENT_QUORUM_FAILURE,
    QEVENT_QUORUM_REPORT_BAD,
    QEVENT_ACPI_OST,

    /* Add to 'monitor_event_names' array in monitor.c when
     * defining new events here */

    QEVENT_MAX,
} MonitorEvent;

int monitor_cur_is_qmp(void);

void monitor_protocol_event(MonitorEvent event, QObject *data);
void monitor_init(CharDriverState *chr, int flags);

int monitor_suspend(Monitor *mon);
void monitor_resume(Monitor *mon);

int monitor_read_bdrv_key_start(Monitor *mon, BlockDriverState *bs,
                                BlockDriverCompletionFunc *completion_cb,
                                void *opaque);
int monitor_read_block_device_key(Monitor *mon, const char *device,
                                  BlockDriverCompletionFunc *completion_cb,
                                  void *opaque);

int monitor_get_fd(Monitor *mon, const char *fdname, Error **errp);
int monitor_handle_fd_param(Monitor *mon, const char *fdname);
int monitor_handle_fd_param2(Monitor *mon, const char *fdname, Error **errp);

void monitor_vprintf(Monitor *mon, const char *fmt, va_list ap)
    GCC_FMT_ATTR(2, 0);
void monitor_printf(Monitor *mon, const char *fmt, ...) GCC_FMT_ATTR(2, 3);
void monitor_flush(Monitor *mon);
int monitor_set_cpu(int cpu_index);
int monitor_get_cpu_index(void);

typedef void (MonitorCompletion)(void *opaque, QObject *ret_data);

void monitor_set_error(Monitor *mon, QError *qerror);
void monitor_read_command(Monitor *mon, int show_prompt);
ReadLineState *monitor_get_rs(Monitor *mon);
int monitor_read_password(Monitor *mon, ReadLineFunc *readline_func,
                          void *opaque);

int qmp_qom_set(Monitor *mon, const QDict *qdict, QObject **ret);

int qmp_qom_get(Monitor *mon, const QDict *qdict, QObject **ret);
int qmp_object_add(Monitor *mon, const QDict *qdict, QObject **ret);
void object_add(const char *type, const char *id, const QDict *qdict,
                Visitor *v, Error **errp);

AddfdInfo *monitor_fdset_add_fd(int fd, bool has_fdset_id, int64_t fdset_id,
                                bool has_opaque, const char *opaque,
                                Error **errp);
int monitor_fdset_get_fd(int64_t fdset_id, int flags);
int monitor_fdset_dup_fd_add(int64_t fdset_id, int dup_fd);
int monitor_fdset_dup_fd_remove(int dup_fd);
int monitor_fdset_dup_fd_find(int dup_fd);

#endif /* !MONITOR_H */
