/*
 * Human Monitor Interface
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef HMP_H
#define HMP_H

#include "qemu/readline.h"
#include "qapi/qapi-types-common.h"
#include "monitor/monitor.h"

#define TYPE_MONITOR_HMP "monitor-hmp"
OBJECT_DECLARE_TYPE(MonitorHMP, MonitorHMPClass, MONITOR_HMP);

#define HMP_STUB(cmd) \
    void hmp_##cmd(Monitor *mon, const QDict *qdict) \
    { \
        g_assert_not_reached(); \
    }

struct MonitorDef {
    const char *name;
    int offset;
    int64_t (*get_value)(Monitor *mon, const MonitorDef *md, int offset);
};

void monitor_new_hmp(const char *id, const char *chardev_id,
                     bool use_readline, Error **errp);

int monitor_vprintf(Monitor *mon, const char *fmt, va_list ap)
    G_GNUC_PRINTF(2, 0);
int monitor_printf(Monitor *mon, const char *fmt, ...) G_GNUC_PRINTF(2, 3);
void monitor_printc(Monitor *mon, int ch);

void monitor_hmp_read_command(MonitorHMP *hmp, int show_prompt);
int monitor_hmp_read_password(MonitorHMP *hmp, ReadLineFunc *readline_func,
                              void *opaque);

void monitor_register_hmp(const char *name, bool info,
                          void (*cmd)(Monitor *mon, const QDict *qdict));
void monitor_register_hmp_info_hrt(const char *name,
                                   HumanReadableText *(*handler)(Error **errp));


CPUArchState *mon_get_cpu_env(Monitor *mon);
CPUState *mon_get_cpu(Monitor *mon);

bool hmp_handle_error(Monitor *mon, Error *err);
void hmp_help_cmd(Monitor *mon, const char *name);
strList *hmp_split_at_comma(const char *str);

void hmp_info_name(Monitor *mon, const QDict *qdict);
void hmp_info_version(Monitor *mon, const QDict *qdict);
void hmp_info_kvm(Monitor *mon, const QDict *qdict);
void hmp_info_accelerators(Monitor *mon, const QDict *qdict);
void hmp_info_status(Monitor *mon, const QDict *qdict);
void hmp_info_uuid(Monitor *mon, const QDict *qdict);
void hmp_info_chardev(Monitor *mon, const QDict *qdict);
void hmp_info_mice(Monitor *mon, const QDict *qdict);
void hmp_info_migrate(Monitor *mon, const QDict *qdict);
void hmp_info_migrate_capabilities(Monitor *mon, const QDict *qdict);
void hmp_info_migrate_parameters(Monitor *mon, const QDict *qdict);
void hmp_info_cpus(Monitor *mon, const QDict *qdict);
void hmp_info_vnc(Monitor *mon, const QDict *qdict);
void hmp_info_spice(Monitor *mon, const QDict *qdict);
void hmp_info_balloon(Monitor *mon, const QDict *qdict);
void hmp_info_pci(Monitor *mon, const QDict *qdict);
void hmp_info_tpm(Monitor *mon, const QDict *qdict);
void hmp_info_iothreads(Monitor *mon, const QDict *qdict);
void hmp_quit(Monitor *mon, const QDict *qdict);
void hmp_stop(Monitor *mon, const QDict *qdict);
void hmp_sync_profile(Monitor *mon, const QDict *qdict);
void hmp_system_reset(Monitor *mon, const QDict *qdict);
void hmp_system_powerdown(Monitor *mon, const QDict *qdict);
void hmp_exit_preconfig(Monitor *mon, const QDict *qdict);
void hmp_announce_self(Monitor *mon, const QDict *qdict);
void hmp_cpu(Monitor *mon, const QDict *qdict);
void hmp_memsave(Monitor *mon, const QDict *qdict);
void hmp_pmemsave(Monitor *mon, const QDict *qdict);
void hmp_ringbuf_write(Monitor *mon, const QDict *qdict);
void hmp_ringbuf_read(Monitor *mon, const QDict *qdict);
void hmp_cont(Monitor *mon, const QDict *qdict);
void hmp_system_wakeup(Monitor *mon, const QDict *qdict);
void hmp_nmi(Monitor *mon, const QDict *qdict);
void hmp_info_network(Monitor *mon, const QDict *qdict);
void hmp_set_link(Monitor *mon, const QDict *qdict);
void hmp_balloon(Monitor *mon, const QDict *qdict);
void hmp_loadvm(Monitor *mon, const QDict *qdict);
void hmp_savevm(Monitor *mon, const QDict *qdict);
void hmp_delvm(Monitor *mon, const QDict *qdict);
void hmp_migrate_cancel(Monitor *mon, const QDict *qdict);
void hmp_migrate_continue(Monitor *mon, const QDict *qdict);
void hmp_migrate_incoming(Monitor *mon, const QDict *qdict);
void hmp_migrate_recover(Monitor *mon, const QDict *qdict);
void hmp_migrate_pause(Monitor *mon, const QDict *qdict);
void hmp_migrate_set_capability(Monitor *mon, const QDict *qdict);
void hmp_migrate_set_parameter(Monitor *mon, const QDict *qdict);
void hmp_client_migrate_info(Monitor *mon, const QDict *qdict);
void hmp_migrate_start_postcopy(Monitor *mon, const QDict *qdict);
void hmp_x_colo_lost_heartbeat(Monitor *mon, const QDict *qdict);
void hmp_set_password(Monitor *mon, const QDict *qdict);
void hmp_expire_password(Monitor *mon, const QDict *qdict);
void hmp_change(Monitor *mon, const QDict *qdict);
#ifdef CONFIG_VNC
void hmp_change_vnc(Monitor *mon, const char *device, const char *target,
                    const char *arg, const char *read_only, bool force,
                    Error **errp);
#endif
void hmp_change_medium(Monitor *mon, const char *device, const char *target,
                       const char *arg, const char *read_only, bool force,
                       Error **errp);
void hmp_migrate(Monitor *mon, const QDict *qdict);
void hmp_device_add(Monitor *mon, const QDict *qdict);
void hmp_device_del(Monitor *mon, const QDict *qdict);
void hmp_dump_guest_memory(Monitor *mon, const QDict *qdict);
void hmp_netdev_add(Monitor *mon, const QDict *qdict);
void hmp_netdev_del(Monitor *mon, const QDict *qdict);
void hmp_getfd(Monitor *mon, const QDict *qdict);
void hmp_closefd(Monitor *mon, const QDict *qdict);
void hmp_mouse_move(Monitor *mon, const QDict *qdict);
void hmp_mouse_button(Monitor *mon, const QDict *qdict);
void hmp_mouse_set(Monitor *mon, const QDict *qdict);
void hmp_sendkey(Monitor *mon, const QDict *qdict);
void coroutine_fn hmp_screendump(Monitor *mon, const QDict *qdict);
void hmp_chardev_add(Monitor *mon, const QDict *qdict);
void hmp_chardev_change(Monitor *mon, const QDict *qdict);
void hmp_chardev_remove(Monitor *mon, const QDict *qdict);
void hmp_chardev_send_break(Monitor *mon, const QDict *qdict);
void hmp_object_add(Monitor *mon, const QDict *qdict);
void hmp_object_del(Monitor *mon, const QDict *qdict);
void hmp_info_memdev(Monitor *mon, const QDict *qdict);
void hmp_info_memory_devices(Monitor *mon, const QDict *qdict);
void hmp_qom_list(Monitor *mon, const QDict *qdict);
void hmp_qom_get(Monitor *mon, const QDict *qdict);
void hmp_qom_set(Monitor *mon, const QDict *qdict);
void hmp_info_qom_tree(Monitor *mon, const QDict *dict);
void hmp_virtio_query(Monitor *mon, const QDict *qdict);
void hmp_virtio_status(Monitor *mon, const QDict *qdict);
void hmp_virtio_queue_status(Monitor *mon, const QDict *qdict);
void hmp_vhost_queue_status(Monitor *mon, const QDict *qdict);
void hmp_virtio_queue_element(Monitor *mon, const QDict *qdict);
void hmp_xen_event_inject(Monitor *mon, const QDict *qdict);
void hmp_xen_event_list(Monitor *mon, const QDict *qdict);
void hmp_rocker(Monitor *mon, const QDict *qdict);
void hmp_rocker_ports(Monitor *mon, const QDict *qdict);
void hmp_rocker_of_dpa_flows(Monitor *mon, const QDict *qdict);
void hmp_rocker_of_dpa_groups(Monitor *mon, const QDict *qdict);
void hmp_info_dump(Monitor *mon, const QDict *qdict);
void hmp_hotpluggable_cpus(Monitor *mon, const QDict *qdict);
void hmp_info_vm_generation_id(Monitor *mon, const QDict *qdict);
void hmp_info_memory_size_summary(Monitor *mon, const QDict *qdict);
void hmp_info_replay(Monitor *mon, const QDict *qdict);
void hmp_replay_break(Monitor *mon, const QDict *qdict);
void hmp_replay_delete_break(Monitor *mon, const QDict *qdict);
void hmp_replay_seek(Monitor *mon, const QDict *qdict);
void hmp_info_dirty_rate(Monitor *mon, const QDict *qdict);
void hmp_calc_dirty_rate(Monitor *mon, const QDict *qdict);
void hmp_set_vcpu_dirty_limit(Monitor *mon, const QDict *qdict);
void hmp_cancel_vcpu_dirty_limit(Monitor *mon, const QDict *qdict);
void hmp_info_vcpu_dirty_limit(Monitor *mon, const QDict *qdict);
void hmp_human_readable_text_helper(Monitor *mon,
                                    HumanReadableText *(*qmp_handler)(Error **));
void hmp_info_stats(Monitor *mon, const QDict *qdict);
void hmp_one_insn_per_tb(Monitor *mon, const QDict *qdict);
void hmp_watchdog_action(Monitor *mon, const QDict *qdict);
void hmp_pcie_aer_inject_error(Monitor *mon, const QDict *qdict);
void hmp_info_capture(Monitor *mon, const QDict *qdict);
void hmp_stopcapture(Monitor *mon, const QDict *qdict);
void hmp_wavcapture(Monitor *mon, const QDict *qdict);
void hmp_trace_event(Monitor *mon, const QDict *qdict);
void hmp_trace_file(Monitor *mon, const QDict *qdict);
void hmp_info_trace_events(Monitor *mon, const QDict *qdict);
void hmp_help(Monitor *mon, const QDict *qdict);
void hmp_clear(Monitor *mon, const QDict *qdict);
void hmp_info_help(Monitor *mon, const QDict *qdict);
void hmp_info_sync_profile(Monitor *mon, const QDict *qdict);
void hmp_info_history(Monitor *mon, const QDict *qdict);
void hmp_logfile(Monitor *mon, const QDict *qdict);
void hmp_log(Monitor *mon, const QDict *qdict);
void hmp_gdbserver(Monitor *mon, const QDict *qdict);
void hmp_print(Monitor *mon, const QDict *qdict);
void hmp_sum(Monitor *mon, const QDict *qdict);
void hmp_ioport_read(Monitor *mon, const QDict *qdict);
void hmp_ioport_write(Monitor *mon, const QDict *qdict);
void hmp_boot_set(Monitor *mon, const QDict *qdict);
void hmp_info_mtree(Monitor *mon, const QDict *qdict);
void hmp_info_cryptodev(Monitor *mon, const QDict *qdict);
void hmp_dumpdtb(Monitor *mon, const QDict *qdict);
void hmp_info_firmware_log(Monitor *mon, const QDict *qdict);
void hmp_info_mem(Monitor *mon, const QDict *qdict);
void hmp_info_tlb(Monitor *mon, const QDict *qdict);
void hmp_mce(Monitor *mon, const QDict *qdict);
void hmp_info_local_apic(Monitor *mon, const QDict *qdict);
void hmp_info_sev(Monitor *mon, const QDict *qdict);
void hmp_info_sgx(Monitor *mon, const QDict *qdict);
void hmp_info_via(Monitor *mon, const QDict *qdict);
void hmp_memory_dump(Monitor *mon, const QDict *qdict);
void hmp_physical_memory_dump(Monitor *mon, const QDict *qdict);
void hmp_info_registers(Monitor *mon, const QDict *qdict);
void hmp_gva2gpa(Monitor *mon, const QDict *qdict);
void hmp_gpa2hva(Monitor *mon, const QDict *qdict);
void hmp_gpa2hpa(Monitor *mon, const QDict *qdict);

void hmp_dump_skeys(Monitor *mon, const QDict *qdict);
void hmp_info_skeys(Monitor *mon, const QDict *qdict);
void hmp_info_cmma(Monitor *mon, const QDict *qdict);
void hmp_migrationmode(Monitor *mon, const QDict *qdict);

#endif
