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
    void hmp_##cmd(MonitorHMP *hmp, const QDict *qdict) \
    { \
        g_assert_not_reached(); \
    }

struct MonitorDef {
    const char *name;
    int offset;
    int64_t (*get_value)(MonitorHMP *hmp, const MonitorDef *md, int offset);
};

void monitor_new_hmp(const char *id, const char *chardev_id,
                     bool use_readline, Error **errp);

MonitorHMP *monitor_cur_hmp(void);

int monitor_hmp_vprintf(MonitorHMP *mon, const char *fmt, va_list ap)
    G_GNUC_PRINTF(2, 0);
int monitor_hmp_printf(MonitorHMP *mon, const char *fmt, ...) G_GNUC_PRINTF(2, 3);
void monitor_hmp_printc(MonitorHMP *mon, int ch);

void monitor_hmp_read_command(MonitorHMP *hmp, int show_prompt);
int monitor_hmp_read_password(MonitorHMP *hmp, ReadLineFunc *readline_func,
                              void *opaque);

void monitor_register_hmp(const char *name, bool info,
                          void (*cmd)(MonitorHMP *mon, const QDict *qdict));
void monitor_register_hmp_info_hrt(const char *name,
                                   HumanReadableText *(*handler)(Error **errp));


CPUArchState *monitor_hmp_get_cpu_env(MonitorHMP *hmp);
CPUState *monitor_hmp_get_cpu(MonitorHMP *hmp);
int monitor_hmp_get_cpu_index(MonitorHMP *hmp);

bool hmp_handle_error(MonitorHMP *hmp, Error *err);
void hmp_help_cmd(MonitorHMP *hmp, const char *name);
strList *hmp_split_at_comma(const char *str);

void hmp_info_name(MonitorHMP *hmp, const QDict *qdict);
void hmp_info_version(MonitorHMP *hmp, const QDict *qdict);
void hmp_info_kvm(MonitorHMP *hmp, const QDict *qdict);
void hmp_info_accelerators(MonitorHMP *hmp, const QDict *qdict);
void hmp_info_status(MonitorHMP *hmp, const QDict *qdict);
void hmp_info_uuid(MonitorHMP *hmp, const QDict *qdict);
void hmp_info_chardev(MonitorHMP *hmp, const QDict *qdict);
void hmp_info_mice(MonitorHMP *hmp, const QDict *qdict);
void hmp_info_migrate(MonitorHMP *hmp, const QDict *qdict);
void hmp_info_migrate_capabilities(MonitorHMP *hmp, const QDict *qdict);
void hmp_info_migrate_parameters(MonitorHMP *hmp, const QDict *qdict);
void hmp_info_cpus(MonitorHMP *hmp, const QDict *qdict);
void hmp_info_vnc(MonitorHMP *hmp, const QDict *qdict);
void hmp_info_spice(MonitorHMP *hmp, const QDict *qdict);
void hmp_info_balloon(MonitorHMP *hmp, const QDict *qdict);
void hmp_info_pci(MonitorHMP *hmp, const QDict *qdict);
void hmp_info_tpm(MonitorHMP *hmp, const QDict *qdict);
void hmp_info_iothreads(MonitorHMP *hmp, const QDict *qdict);
void hmp_quit(MonitorHMP *hmp, const QDict *qdict);
void hmp_stop(MonitorHMP *hmp, const QDict *qdict);
void hmp_sync_profile(MonitorHMP *hmp, const QDict *qdict);
void hmp_system_reset(MonitorHMP *hmp, const QDict *qdict);
void hmp_system_powerdown(MonitorHMP *hmp, const QDict *qdict);
void hmp_exit_preconfig(MonitorHMP *hmp, const QDict *qdict);
void hmp_announce_self(MonitorHMP *hmp, const QDict *qdict);
void hmp_cpu(MonitorHMP *hmp, const QDict *qdict);
void hmp_memsave(MonitorHMP *hmp, const QDict *qdict);
void hmp_pmemsave(MonitorHMP *hmp, const QDict *qdict);
void hmp_ringbuf_write(MonitorHMP *hmp, const QDict *qdict);
void hmp_ringbuf_read(MonitorHMP *hmp, const QDict *qdict);
void hmp_cont(MonitorHMP *hmp, const QDict *qdict);
void hmp_system_wakeup(MonitorHMP *hmp, const QDict *qdict);
void hmp_nmi(MonitorHMP *hmp, const QDict *qdict);
void hmp_info_network(MonitorHMP *hmp, const QDict *qdict);
void hmp_set_link(MonitorHMP *hmp, const QDict *qdict);
void hmp_balloon(MonitorHMP *hmp, const QDict *qdict);
void hmp_loadvm(MonitorHMP *hmp, const QDict *qdict);
void hmp_savevm(MonitorHMP *hmp, const QDict *qdict);
void hmp_delvm(MonitorHMP *hmp, const QDict *qdict);
void hmp_migrate_cancel(MonitorHMP *hmp, const QDict *qdict);
void hmp_migrate_continue(MonitorHMP *hmp, const QDict *qdict);
void hmp_migrate_incoming(MonitorHMP *hmp, const QDict *qdict);
void hmp_migrate_recover(MonitorHMP *hmp, const QDict *qdict);
void hmp_migrate_pause(MonitorHMP *hmp, const QDict *qdict);
void hmp_migrate_set_capability(MonitorHMP *hmp, const QDict *qdict);
void hmp_migrate_set_parameter(MonitorHMP *hmp, const QDict *qdict);
void hmp_client_migrate_info(MonitorHMP *hmp, const QDict *qdict);
void hmp_migrate_start_postcopy(MonitorHMP *hmp, const QDict *qdict);
void hmp_x_colo_lost_heartbeat(MonitorHMP *hmp, const QDict *qdict);
void hmp_set_password(MonitorHMP *hmp, const QDict *qdict);
void hmp_expire_password(MonitorHMP *hmp, const QDict *qdict);
void hmp_change(MonitorHMP *hmp, const QDict *qdict);
#ifdef CONFIG_VNC
void hmp_change_vnc(MonitorHMP *hmp, const char *device, const char *target,
                    const char *arg, const char *read_only, bool force,
                    Error **errp);
#endif
void hmp_change_medium(MonitorHMP *hmp, const char *device, const char *target,
                       const char *arg, const char *read_only, bool force,
                       Error **errp);
void hmp_migrate(MonitorHMP *hmp, const QDict *qdict);
void hmp_device_add(MonitorHMP *hmp, const QDict *qdict);
void hmp_device_del(MonitorHMP *hmp, const QDict *qdict);
void hmp_dump_guest_memory(MonitorHMP *hmp, const QDict *qdict);
void hmp_netdev_add(MonitorHMP *hmp, const QDict *qdict);
void hmp_netdev_del(MonitorHMP *hmp, const QDict *qdict);
void hmp_getfd(MonitorHMP *hmp, const QDict *qdict);
void hmp_closefd(MonitorHMP *hmp, const QDict *qdict);
void hmp_mouse_move(MonitorHMP *hmp, const QDict *qdict);
void hmp_mouse_button(MonitorHMP *hmp, const QDict *qdict);
void hmp_mouse_set(MonitorHMP *hmp, const QDict *qdict);
void hmp_sendkey(MonitorHMP *hmp, const QDict *qdict);
void coroutine_fn hmp_screendump(MonitorHMP *hmp, const QDict *qdict);
void hmp_chardev_add(MonitorHMP *hmp, const QDict *qdict);
void hmp_chardev_change(MonitorHMP *hmp, const QDict *qdict);
void hmp_chardev_remove(MonitorHMP *hmp, const QDict *qdict);
void hmp_chardev_send_break(MonitorHMP *hmp, const QDict *qdict);
void hmp_object_add(MonitorHMP *hmp, const QDict *qdict);
void hmp_object_del(MonitorHMP *hmp, const QDict *qdict);
void hmp_info_memdev(MonitorHMP *hmp, const QDict *qdict);
void hmp_info_memory_devices(MonitorHMP *hmp, const QDict *qdict);
void hmp_qom_list(MonitorHMP *hmp, const QDict *qdict);
void hmp_qom_get(MonitorHMP *hmp, const QDict *qdict);
void hmp_qom_set(MonitorHMP *hmp, const QDict *qdict);
void hmp_info_qom_tree(MonitorHMP *hmp, const QDict *dict);
void hmp_virtio_query(MonitorHMP *hmp, const QDict *qdict);
void hmp_virtio_status(MonitorHMP *hmp, const QDict *qdict);
void hmp_virtio_queue_status(MonitorHMP *hmp, const QDict *qdict);
void hmp_vhost_queue_status(MonitorHMP *hmp, const QDict *qdict);
void hmp_virtio_queue_element(MonitorHMP *hmp, const QDict *qdict);
void hmp_xen_event_inject(MonitorHMP *hmp, const QDict *qdict);
void hmp_xen_event_list(MonitorHMP *hmp, const QDict *qdict);
void hmp_rocker(MonitorHMP *hmp, const QDict *qdict);
void hmp_rocker_ports(MonitorHMP *hmp, const QDict *qdict);
void hmp_rocker_of_dpa_flows(MonitorHMP *hmp, const QDict *qdict);
void hmp_rocker_of_dpa_groups(MonitorHMP *hmp, const QDict *qdict);
void hmp_info_dump(MonitorHMP *hmp, const QDict *qdict);
void hmp_hotpluggable_cpus(MonitorHMP *hmp, const QDict *qdict);
void hmp_info_vm_generation_id(MonitorHMP *hmp, const QDict *qdict);
void hmp_info_memory_size_summary(MonitorHMP *hmp, const QDict *qdict);
void hmp_info_replay(MonitorHMP *hmp, const QDict *qdict);
void hmp_replay_break(MonitorHMP *hmp, const QDict *qdict);
void hmp_replay_delete_break(MonitorHMP *hmp, const QDict *qdict);
void hmp_replay_seek(MonitorHMP *hmp, const QDict *qdict);
void hmp_info_dirty_rate(MonitorHMP *hmp, const QDict *qdict);
void hmp_calc_dirty_rate(MonitorHMP *hmp, const QDict *qdict);
void hmp_set_vcpu_dirty_limit(MonitorHMP *hmp, const QDict *qdict);
void hmp_cancel_vcpu_dirty_limit(MonitorHMP *hmp, const QDict *qdict);
void hmp_info_vcpu_dirty_limit(MonitorHMP *hmp, const QDict *qdict);
void hmp_human_readable_text_helper(Monitor *mon,
                                    HumanReadableText *(*qmp_handler)(Error **));
void hmp_info_stats(MonitorHMP *hmp, const QDict *qdict);
void hmp_one_insn_per_tb(MonitorHMP *hmp, const QDict *qdict);
void hmp_watchdog_action(MonitorHMP *hmp, const QDict *qdict);
void hmp_pcie_aer_inject_error(MonitorHMP *hmp, const QDict *qdict);
void hmp_info_capture(MonitorHMP *hmp, const QDict *qdict);
void hmp_stopcapture(MonitorHMP *hmp, const QDict *qdict);
void hmp_wavcapture(MonitorHMP *hmp, const QDict *qdict);
void hmp_trace_event(MonitorHMP *hmp, const QDict *qdict);
void hmp_trace_file(MonitorHMP *hmp, const QDict *qdict);
void hmp_info_trace_events(MonitorHMP *hmp, const QDict *qdict);
void hmp_help(MonitorHMP *hmp, const QDict *qdict);
void hmp_clear(MonitorHMP *hmp, const QDict *qdict);
void hmp_info_help(MonitorHMP *hmp, const QDict *qdict);
void hmp_info_sync_profile(MonitorHMP *hmp, const QDict *qdict);
void hmp_info_history(MonitorHMP *hmp, const QDict *qdict);
void hmp_logfile(MonitorHMP *hmp, const QDict *qdict);
void hmp_log(MonitorHMP *hmp, const QDict *qdict);
void hmp_gdbserver(MonitorHMP *hmp, const QDict *qdict);
void hmp_print(MonitorHMP *hmp, const QDict *qdict);
void hmp_sum(MonitorHMP *hmp, const QDict *qdict);
void hmp_ioport_read(MonitorHMP *hmp, const QDict *qdict);
void hmp_ioport_write(MonitorHMP *hmp, const QDict *qdict);
void hmp_boot_set(MonitorHMP *hmp, const QDict *qdict);
void hmp_info_mtree(MonitorHMP *hmp, const QDict *qdict);
void hmp_info_cryptodev(MonitorHMP *hmp, const QDict *qdict);
void hmp_dumpdtb(MonitorHMP *hmp, const QDict *qdict);
void hmp_info_firmware_log(MonitorHMP *hmp, const QDict *qdict);
void hmp_info_mem(MonitorHMP *hmp, const QDict *qdict);
void hmp_info_tlb(MonitorHMP *hmp, const QDict *qdict);
void hmp_mce(MonitorHMP *hmp, const QDict *qdict);
void hmp_info_local_apic(MonitorHMP *hmp, const QDict *qdict);
void hmp_info_sev(MonitorHMP *hmp, const QDict *qdict);
void hmp_info_sgx(MonitorHMP *hmp, const QDict *qdict);
void hmp_info_via(MonitorHMP *hmp, const QDict *qdict);
void hmp_memory_dump(MonitorHMP *hmp, const QDict *qdict);
void hmp_physical_memory_dump(MonitorHMP *hmp, const QDict *qdict);
void hmp_info_registers(MonitorHMP *hmp, const QDict *qdict);
void hmp_gva2gpa(MonitorHMP *hmp, const QDict *qdict);
void hmp_gpa2hva(MonitorHMP *hmp, const QDict *qdict);
void hmp_gpa2hpa(MonitorHMP *hmp, const QDict *qdict);

void hmp_dump_skeys(MonitorHMP *hmp, const QDict *qdict);
void hmp_info_skeys(MonitorHMP *hmp, const QDict *qdict);
void hmp_info_cmma(MonitorHMP *hmp, const QDict *qdict);
void hmp_migrationmode(MonitorHMP *hmp, const QDict *qdict);

#endif
