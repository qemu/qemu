/*
 * gdb server stub - system specific bits
 *
 * Debug integration depends on support from the individual
 * accelerators so most of this involves calling the ops helpers.
 *
 * Copyright (c) 2003-2005 Fabrice Bellard
 * Copyright (c) 2022 Linaro Ltd
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/cutils.h"
#include "exec/gdbstub.h"
#include "gdbstub/syscalls.h"
#include "gdbstub/commands.h"
#include "exec/hwaddr.h"
#include "accel/accel-ops.h"
#include "accel/accel-cpu-ops.h"
#include "system/cpus.h"
#include "system/runstate.h"
#include "system/replay.h"
#include "hw/core/cpu.h"
#include "hw/cpu/cluster.h"
#include "hw/boards.h"
#include "chardev/char.h"
#include "chardev/char-fe.h"
#include "monitor/monitor.h"
#include "trace.h"
#include "internals.h"

/* System emulation specific state */
typedef struct {
    CharFrontend chr;
    Chardev *mon_chr;
} GDBSystemState;

GDBSystemState gdbserver_system_state;

static void reset_gdbserver_state(void)
{
    g_free(gdbserver_state.processes);
    gdbserver_state.processes = NULL;
    gdbserver_state.process_num = 0;
    gdbserver_state.allow_stop_reply = false;
}

/*
 * Return the GDB index for a given vCPU state.
 *
 * In system mode GDB numbers CPUs from 1 as 0 is reserved as an "any
 * cpu" index.
 */
int gdb_get_cpu_index(CPUState *cpu)
{
    return cpu->cpu_index + 1;
}

/*
 * We check the status of the last message in the chardev receive code
 */
bool gdb_got_immediate_ack(void)
{
    return true;
}

/*
 * GDB Connection management. For system emulation we do all of this
 * via our existing Chardev infrastructure which allows us to support
 * network and unix sockets.
 */

void gdb_put_buffer(const uint8_t *buf, int len)
{
    /*
     * XXX this blocks entire thread. Rewrite to use
     * qemu_chr_fe_write and background I/O callbacks
     */
    qemu_chr_fe_write_all(&gdbserver_system_state.chr, buf, len);
}

static void gdb_chr_event(void *opaque, QEMUChrEvent event)
{
    int i;
    GDBState *s = (GDBState *) opaque;

    switch (event) {
    case CHR_EVENT_OPENED:
        /* Start with first process attached, others detached */
        for (i = 0; i < s->process_num; i++) {
            s->processes[i].attached = !i;
        }

        s->c_cpu = gdb_first_attached_cpu();
        s->g_cpu = s->c_cpu;

        vm_stop(RUN_STATE_PAUSED);
        replay_gdb_attached();
        break;
    default:
        break;
    }
}

/*
 * In system-mode we stop the VM and wait to send the syscall packet
 * until notification that the CPU has stopped. This must be done
 * because if the packet is sent now the reply from the syscall
 * request could be received while the CPU is still in the running
 * state, which can cause packets to be dropped and state transition
 * 'T' packets to be sent while the syscall is still being processed.
 */
void gdb_syscall_handling(const char *syscall_packet)
{
    vm_stop(RUN_STATE_DEBUG);
    qemu_cpu_kick(gdbserver_state.c_cpu);
}

static void gdb_vm_state_change(void *opaque, bool running, RunState state)
{
    CPUState *cpu = gdbserver_state.c_cpu;
    g_autoptr(GString) buf = g_string_new(NULL);
    g_autoptr(GString) tid = g_string_new(NULL);
    const char *type;
    int ret;

    if (running || gdbserver_state.state == RS_INACTIVE) {
        return;
    }

    /* Is there a GDB syscall waiting to be sent?  */
    if (gdb_handled_syscall()) {
        return;
    }

    if (cpu == NULL) {
        /* No process attached */
        return;
    }

    if (!gdbserver_state.allow_stop_reply) {
        return;
    }

    gdb_append_thread_id(cpu, tid);

    switch (state) {
    case RUN_STATE_DEBUG:
        if (cpu->watchpoint_hit) {
            switch (cpu->watchpoint_hit->flags & BP_MEM_ACCESS) {
            case BP_MEM_READ:
                type = "r";
                break;
            case BP_MEM_ACCESS:
                type = "a";
                break;
            default:
                type = "";
                break;
            }
            trace_gdbstub_hit_watchpoint(type,
                                         gdb_get_cpu_index(cpu),
                                         cpu->watchpoint_hit->vaddr);
            g_string_printf(buf, "T%02xthread:%s;%swatch:%" VADDR_PRIx ";",
                            GDB_SIGNAL_TRAP, tid->str, type,
                            cpu->watchpoint_hit->vaddr);
            cpu->watchpoint_hit = NULL;
            goto send_packet;
        } else {
            trace_gdbstub_hit_break();
        }
        ret = GDB_SIGNAL_TRAP;
        break;
    case RUN_STATE_PAUSED:
        trace_gdbstub_hit_paused();
        ret = GDB_SIGNAL_INT;
        break;
    case RUN_STATE_SHUTDOWN:
        trace_gdbstub_hit_shutdown();
        ret = GDB_SIGNAL_QUIT;
        break;
    case RUN_STATE_IO_ERROR:
        trace_gdbstub_hit_io_error();
        ret = GDB_SIGNAL_STOP;
        break;
    case RUN_STATE_WATCHDOG:
        trace_gdbstub_hit_watchdog();
        ret = GDB_SIGNAL_ALRM;
        break;
    case RUN_STATE_INTERNAL_ERROR:
        trace_gdbstub_hit_internal_error();
        ret = GDB_SIGNAL_ABRT;
        break;
    case RUN_STATE_SAVE_VM:
    case RUN_STATE_RESTORE_VM:
        return;
    case RUN_STATE_FINISH_MIGRATE:
        ret = GDB_SIGNAL_XCPU;
        break;
    default:
        trace_gdbstub_hit_unknown(state);
        ret = GDB_SIGNAL_UNKNOWN;
        break;
    }
    gdb_set_stop_cpu(cpu);
    g_string_printf(buf, "T%02xthread:%s;", ret, tid->str);

send_packet:
    gdb_put_packet(buf->str);
    gdbserver_state.allow_stop_reply = false;

    /* disable single step if it was enabled */
    cpu_single_step(cpu, 0);
}

#ifndef _WIN32
static void gdb_sigterm_handler(int signal)
{
    if (runstate_is_running()) {
        vm_stop(RUN_STATE_PAUSED);
    }
}
#endif

static int gdb_monitor_write(Chardev *chr, const uint8_t *buf, int len)
{
    g_autoptr(GString) hex_buf = g_string_new("O");
    gdb_memtohex(hex_buf, buf, len);
    gdb_put_packet(hex_buf->str);
    return len;
}

static void gdb_monitor_open(Chardev *chr, ChardevBackend *backend,
                             bool *be_opened, Error **errp)
{
    *be_opened = false;
}

static void char_gdb_class_init(ObjectClass *oc, const void *data)
{
    ChardevClass *cc = CHARDEV_CLASS(oc);

    cc->internal = true;
    cc->open = gdb_monitor_open;
    cc->chr_write = gdb_monitor_write;
}

#define TYPE_CHARDEV_GDB "chardev-gdb"

static const TypeInfo char_gdb_type_info = {
    .name = TYPE_CHARDEV_GDB,
    .parent = TYPE_CHARDEV,
    .class_init = char_gdb_class_init,
};

static int gdb_chr_can_receive(void *opaque)
{
  /*
   * We can handle an arbitrarily large amount of data.
   * Pick the maximum packet size, which is as good as anything.
   */
  return MAX_PACKET_LENGTH;
}

static void gdb_chr_receive(void *opaque, const uint8_t *buf, int size)
{
    int i;

    for (i = 0; i < size; i++) {
        gdb_read_byte(buf[i]);
    }
}

static int find_cpu_clusters(Object *child, void *opaque)
{
    if (object_dynamic_cast(child, TYPE_CPU_CLUSTER)) {
        GDBState *s = (GDBState *) opaque;
        CPUClusterState *cluster = CPU_CLUSTER(child);
        GDBProcess *process;

        s->processes = g_renew(GDBProcess, s->processes, ++s->process_num);

        process = &s->processes[s->process_num - 1];

        /*
         * GDB process IDs -1 and 0 are reserved. To avoid subtle errors at
         * runtime, we enforce here that the machine does not use a cluster ID
         * that would lead to PID 0.
         */
        assert(cluster->cluster_id != UINT32_MAX);
        process->pid = cluster->cluster_id + 1;
        process->attached = false;
        process->target_xml = NULL;

        return 0;
    }

    return object_child_foreach(child, find_cpu_clusters, opaque);
}

static int pid_order(const void *a, const void *b)
{
    GDBProcess *pa = (GDBProcess *) a;
    GDBProcess *pb = (GDBProcess *) b;

    if (pa->pid < pb->pid) {
        return -1;
    } else if (pa->pid > pb->pid) {
        return 1;
    } else {
        return 0;
    }
}

static void create_processes(GDBState *s)
{
    object_child_foreach(object_get_root(), find_cpu_clusters, s);

    if (gdbserver_state.processes) {
        /* Sort by PID */
        qsort(gdbserver_state.processes,
              gdbserver_state.process_num,
              sizeof(gdbserver_state.processes[0]),
              pid_order);
    }

    gdb_create_default_process(s);
}

bool gdbserver_start(const char *device, Error **errp)
{
    Chardev *chr = NULL;
    Chardev *mon_chr;
    g_autoptr(GString) cs = g_string_new(device);

    if (!first_cpu) {
        error_setg(errp, "gdbstub: meaningless to attach gdb to a "
                   "machine without any CPU.");
        return false;
    }

    if (!gdb_supports_guest_debug()) {
        error_setg(errp, "gdbstub: current accelerator doesn't "
                   "support guest debugging");
        return false;
    }

    if (cs->len == 0) {
        error_setg(errp, "gdbstub: missing connection string");
        return false;
    }

    trace_gdbstub_op_start(cs->str);

    if (g_strcmp0(cs->str, "none") != 0) {
        if (g_str_has_prefix(cs->str, "tcp:")) {
            /* enforce required TCP attributes */
            g_string_append_printf(cs, ",wait=off,nodelay=on,server=on");
        }
#ifndef _WIN32
        else if (strcmp(device, "stdio") == 0) {
            struct sigaction act;

            memset(&act, 0, sizeof(act));
            act.sa_handler = gdb_sigterm_handler;
            sigaction(SIGINT, &act, NULL);
        }
#endif
        /*
         * FIXME: it's a bit weird to allow using a mux chardev here
         * and implicitly setup a monitor. We may want to break this.
         */
        chr = qemu_chr_new_noreplay("gdb", cs->str, true, NULL);
        if (!chr) {
            error_setg(errp, "gdbstub: couldn't create chardev");
            return false;
        }
    }

    if (!gdbserver_state.init) {
        gdb_init_gdbserver_state();

        qemu_add_vm_change_state_handler(gdb_vm_state_change, NULL);

        /* Initialize a monitor terminal for gdb */
        mon_chr = qemu_chardev_new(NULL, TYPE_CHARDEV_GDB,
                                   NULL, NULL, &error_abort);
        monitor_init_hmp(mon_chr, false, &error_abort);
    } else {
        qemu_chr_fe_deinit(&gdbserver_system_state.chr, true);
        mon_chr = gdbserver_system_state.mon_chr;
        reset_gdbserver_state();
    }

    create_processes(&gdbserver_state);

    if (chr) {
        qemu_chr_fe_init(&gdbserver_system_state.chr, chr, &error_abort);
        qemu_chr_fe_set_handlers(&gdbserver_system_state.chr,
                                 gdb_chr_can_receive,
                                 gdb_chr_receive, gdb_chr_event,
                                 NULL, &gdbserver_state, NULL, true);
    }
    gdbserver_state.state = chr ? RS_IDLE : RS_INACTIVE;
    gdbserver_system_state.mon_chr = mon_chr;
    gdb_syscall_reset();

    return true;
}

static void register_types(void)
{
    type_register_static(&char_gdb_type_info);
}

type_init(register_types);

/* Tell the remote gdb that the process has exited.  */
void gdb_exit(int code)
{
    char buf[4];

    if (!gdbserver_state.init) {
        return;
    }

    trace_gdbstub_op_exiting((uint8_t)code);

    if (gdbserver_state.allow_stop_reply) {
        snprintf(buf, sizeof(buf), "W%02x", (uint8_t)code);
        gdb_put_packet(buf);
        gdbserver_state.allow_stop_reply = false;
    }

    qemu_chr_fe_deinit(&gdbserver_system_state.chr, true);
}

void gdb_qemu_exit(int code)
{
    qemu_system_shutdown_request_with_code(SHUTDOWN_CAUSE_GUEST_SHUTDOWN,
                                           code);
}

/*
 * Memory access
 */
static int phy_memory_mode;

int gdb_target_memory_rw_debug(CPUState *cpu, hwaddr addr,
                               uint8_t *buf, int len, bool is_write)
{
    if (phy_memory_mode) {
        if (is_write) {
            cpu_physical_memory_write(addr, buf, len);
        } else {
            cpu_physical_memory_read(addr, buf, len);
        }
        return 0;
    }

    if (cpu->cc->memory_rw_debug) {
        return cpu->cc->memory_rw_debug(cpu, addr, buf, len, is_write);
    }

    return cpu_memory_rw_debug(cpu, addr, buf, len, is_write);
}

/*
 * cpu helpers
 */

unsigned int gdb_get_max_cpus(void)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    return ms->smp.max_cpus;
}

bool gdb_can_reverse(void)
{
    return replay_mode == REPLAY_MODE_PLAY;
}

/*
 * Softmmu specific command helpers
 */

void gdb_handle_query_qemu_phy_mem_mode(GArray *params,
                                        void *ctx)
{
    g_string_printf(gdbserver_state.str_buf, "%d", phy_memory_mode);
    gdb_put_strbuf();
}

void gdb_handle_set_qemu_phy_mem_mode(GArray *params, void *ctx)
{
    if (!params->len) {
        gdb_put_packet("E22");
        return;
    }

    if (!gdb_get_cmd_param(params, 0)->val_ul) {
        phy_memory_mode = 0;
    } else {
        phy_memory_mode = 1;
    }
    gdb_put_packet("OK");
}

void gdb_handle_query_rcmd(GArray *params, void *ctx)
{
    const guint8 zero = 0;
    int len;

    if (!params->len) {
        gdb_put_packet("E22");
        return;
    }

    len = strlen(gdb_get_cmd_param(params, 0)->data);
    if (len % 2) {
        gdb_put_packet("E01");
        return;
    }

    g_assert(gdbserver_state.mem_buf->len == 0);
    len = len / 2;
    gdb_hextomem(gdbserver_state.mem_buf, gdb_get_cmd_param(params, 0)->data, len);
    g_byte_array_append(gdbserver_state.mem_buf, &zero, 1);
    qemu_chr_be_write(gdbserver_system_state.mon_chr,
                      gdbserver_state.mem_buf->data,
                      gdbserver_state.mem_buf->len);
    gdb_put_packet("OK");
}

/*
 * Execution state helpers
 */

void gdb_handle_query_attached(GArray *params, void *ctx)
{
    gdb_put_packet("1");
}

void gdb_continue(void)
{
    if (!runstate_needs_reset()) {
        trace_gdbstub_op_continue();
        vm_start();
    }
}

/*
 * Resume execution, per CPU actions.
 */
int gdb_continue_partial(char *newstates)
{
    CPUState *cpu;
    int res = 0;
    int flag = 0;

    if (!runstate_needs_reset()) {
        bool step_requested = false;
        CPU_FOREACH(cpu) {
            if (newstates[cpu->cpu_index] == 's') {
                step_requested = true;
                break;
            }
        }

        if (vm_prepare_start(step_requested)) {
            return 0;
        }

        CPU_FOREACH(cpu) {
            switch (newstates[cpu->cpu_index]) {
            case 0:
            case 1:
                break; /* nothing to do here */
            case 's':
                trace_gdbstub_op_stepping(cpu->cpu_index);
                cpu_single_step(cpu, gdbserver_state.sstep_flags);
                cpu_resume(cpu);
                flag = 1;
                break;
            case 'c':
                trace_gdbstub_op_continue_cpu(cpu->cpu_index);
                cpu_resume(cpu);
                flag = 1;
                break;
            default:
                res = -1;
                break;
            }
        }
    }
    if (flag) {
        qemu_clock_enable(QEMU_CLOCK_VIRTUAL, true);
    }
    return res;
}

/*
 * Signal Handling - in system mode we only need SIGINT and SIGTRAP; other
 * signals are not yet supported.
 */

enum {
    TARGET_SIGINT = 2,
    TARGET_SIGTRAP = 5
};

int gdb_signal_to_target(int sig)
{
    switch (sig) {
    case 2:
        return TARGET_SIGINT;
    case 5:
        return TARGET_SIGTRAP;
    default:
        return -1;
    }
}

/*
 * Break/Watch point helpers
 */

bool gdb_supports_guest_debug(void)
{
    const AccelOpsClass *ops = cpus_get_accel();
    if (ops->supports_guest_debug) {
        return ops->supports_guest_debug();
    }
    return false;
}

int gdb_breakpoint_insert(CPUState *cs, int type, vaddr addr, vaddr len)
{
    const AccelOpsClass *ops = cpus_get_accel();
    if (ops->insert_breakpoint) {
        return ops->insert_breakpoint(cs, type, addr, len);
    }
    return -ENOSYS;
}

int gdb_breakpoint_remove(CPUState *cs, int type, vaddr addr, vaddr len)
{
    const AccelOpsClass *ops = cpus_get_accel();
    if (ops->remove_breakpoint) {
        return ops->remove_breakpoint(cs, type, addr, len);
    }
    return -ENOSYS;
}

void gdb_breakpoint_remove_all(CPUState *cs)
{
    const AccelOpsClass *ops = cpus_get_accel();
    if (ops->remove_all_breakpoints) {
        ops->remove_all_breakpoints(cs);
    }
}
