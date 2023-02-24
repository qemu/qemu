#include "qemu/osdep.h"
#include "sysemu/replay.h"
#include "ui/input.h"

void replay_input_event(QemuConsole *src, InputEvent *evt)
{
    qemu_input_event_send_impl(src, evt);
}

void replay_input_sync_event(void)
{
    qemu_input_event_sync_impl();
}

void replay_add_blocker(const char *feature)
{
}
void replay_audio_in(size_t *recorded, void *samples, size_t *wpos, size_t size)
{
}
void replay_audio_out(size_t *played)
{
}
void replay_breakpoint(void)
{
}
bool replay_can_snapshot(void)
{
    return true;
}
void replay_configure(struct QemuOpts *opts)
{
}
void replay_flush_events(void)
{
}
void replay_gdb_attached(void)
{
}
bool replay_running_debug(void)
{
    return false;
}
void replay_shutdown_request(ShutdownCause cause)
{
}
void replay_start(void)
{
}
void replay_vmstate_init(void)
{
}

#include "monitor/monitor.h"
#include "monitor/hmp.h"
#include "qapi/qapi-commands-replay.h"
#include "qapi/error.h"
#include "qemu/error-report.h"

void hmp_info_replay(Monitor *mon, const QDict *qdict)
{
    error_report("replay support not available");
}
void hmp_replay_break(Monitor *mon, const QDict *qdict)
{
    error_report("replay support not available");
}
void hmp_replay_delete_break(Monitor *mon, const QDict *qdict)
{
    error_report("replay support not available");
}
void hmp_replay_seek(Monitor *mon, const QDict *qdict)
{
    error_report("replay support not available");
}
ReplayInfo *qmp_query_replay(Error **errp)
{
    error_set(errp, ERROR_CLASS_COMMAND_NOT_FOUND,
              "replay support not available");
    return NULL;
}
void qmp_replay_break(int64_t icount, Error **errp)
{
    error_set(errp, ERROR_CLASS_COMMAND_NOT_FOUND,
              "replay support not available");
}
void qmp_replay_delete_break(Error **errp)
{
    error_set(errp, ERROR_CLASS_COMMAND_NOT_FOUND,
              "replay support not available");
}
void qmp_replay_seek(int64_t icount, Error **errp)
{
    error_set(errp, ERROR_CLASS_COMMAND_NOT_FOUND,
              "replay support not available");
}
