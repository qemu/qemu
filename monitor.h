#ifndef MONITOR_H
#define MONITOR_H

#include "qemu-common.h"
#include "qemu-char.h"
#include "qerror.h"
#include "qdict.h"
#include "block.h"
#include "readline.h"

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
    QEVENT_DEVICE_TRAY_MOVED,
    QEVENT_SUSPEND,
    QEVENT_WAKEUP,
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

int monitor_get_fd(Monitor *mon, const char *fdname);

void monitor_vprintf(Monitor *mon, const char *fmt, va_list ap)
    GCC_FMT_ATTR(2, 0);
void monitor_printf(Monitor *mon, const char *fmt, ...) GCC_FMT_ATTR(2, 3);
void monitor_print_filename(Monitor *mon, const char *filename);
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

#endif /* !MONITOR_H */
