/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef MONITOR_HMP_INTERNAL_H
#define MONITOR_HMP_INTERNAL_H

#ifdef CONFIG_HMP
#include "monitor/hmp.h"
/*
 * Supported types:
 *
 * 'F'          filename
 * 'B'          block device name
 * 's'          string (accept optional quote)
 * 'S'          it just appends the rest of the string (accept optional quote)
 * 'O'          option string of the form NAME=VALUE,...
 *              parsed according to QemuOptsList given by its name
 *              Example: 'device:O' uses qemu_device_opts.
 *              Restriction: only lists with empty desc are supported
 *              TODO lift the restriction
 * 'i'          32 bit integer
 * 'l'          target long (32 or 64 bit)
 * 'M'          Non-negative target long (32 or 64 bit), in user mode the
 *              value is multiplied by 2^20 (think Mebibyte)
 * 'o'          octets (aka bytes)
 *              user mode accepts an optional E, e, P, p, T, t, G, g, M, m,
 *              K, k suffix, which multiplies the value by 2^60 for suffixes E
 *              and e, 2^50 for suffixes P and p, 2^40 for suffixes T and t,
 *              2^30 for suffixes G and g, 2^20 for M and m, 2^10 for K and k
 * 'T'          double
 *              user mode accepts an optional ms, us, ns suffix,
 *              which divides the value by 1e3, 1e6, 1e9, respectively
 * '/'          optional gdb-like print format (like "/10x")
 *
 * '?'          optional type (for all types, except '/')
 * '.'          other form of optional type (for 'i' and 'l')
 * 'b'          boolean
 *              user mode accepts "on" or "off"
 * '-'          optional parameter (eg. '-f'); if followed by a 's', it
 *              specifies an optional string param (e.g. '-fs' allows '-f foo')
 *
 */

typedef struct HMPCommand {
    const char *name;
    const char *args_type;
    const char *params;
    const char *help;
    const char *flags; /* p=preconfig */
    void (*cmd)(MonitorHMP *hmp, const QDict *qdict);
    /*
     * If implementing a command that takes no arguments and simply
     * prints formatted data, then leave @cmd NULL, and then set
     * @cmd_info_hrt to the corresponding QMP handler that returns
     * the formatted text.
     */
    HumanReadableText *(*cmd_info_hrt)(Error **errp);
    /*
     * @sub_table is a list of 2nd level of commands. If it does not exist,
     * cmd should be used. If it exists, sub_table[?].cmd should be
     * used, and cmd of 1st level plays the role of help function.
     */
    struct HMPCommand *sub_table;
    void (*command_completion)(ReadLineState *rs, int nb_args, const char *str);

    /* Keep non-pointer data at the end to minimize holes. */

    /**
     * @arch_bitmask: bitmask of QEMU_ARCH_* constants
     *     Allow to restrict the command for a particular set of
     *     target architectures.
     */
    uint32_t arch_bitmask;
    bool coroutine;
} HMPCommand;

struct MonitorHMPClass {
    MonitorClass parent_class;
};

struct MonitorHMP {
    Monitor parent_obj;
    bool use_readline;
    /*
     * State used only in the thread "owning" the monitor.
     * This is currently always the main thread, since
     * HMP does not allow use of the I/O thread at this time.
     * These members can be safely accessed without locks.
     */
    ReadLineState *rs;
    char *mon_cpu_path;
    int reset_seen;
};

int monitor_hmp_set_cpu(MonitorHMP *hmp, int cpu_index);
void handle_hmp_command(MonitorHMP *hmp, const char *cmdline);
int hmp_compare_cmd(const char *name, const char *list);

/*
 * hmp_cmds_for_target: Return array of HMPCommand entries
 *
 * If @info_command is true, return the particular 'info foo' commands array.
 */
HMPCommand *hmp_cmds_for_target(bool info_command);

#endif /* CONFIG_HMP */
#endif
