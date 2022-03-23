/*
 * This file is supposed to be included only by .c files. No header file should
 * depend on qemu-common.h, as this would easily lead to circular header
 * dependencies.
 *
 * If a header file uses a definition from qemu-common.h, that definition
 * must be moved to a separate header file, and the header that uses it
 * must include that header.
 */
#ifndef QEMU_COMMON_H
#define QEMU_COMMON_H

/* Copyright string for -version arguments, About dialogs, etc */
#define QEMU_COPYRIGHT "Copyright (c) 2003-2022 " \
    "Fabrice Bellard and the QEMU Project developers"

/* Bug reporting information for --help arguments, About dialogs, etc */
#define QEMU_HELP_BOTTOM \
    "See <https://qemu.org/contribute/report-a-bug> for how to report bugs.\n" \
    "More information on the QEMU project at <https://qemu.org>."

/* main function, renamed */
#if defined(CONFIG_COCOA)
int qemu_main(int argc, char **argv, char **envp);
#endif


void qemu_progress_init(int enabled, float min_skip);
void qemu_progress_end(void);
void qemu_progress_print(float delta, int max);
const char *qemu_get_vm_name(void);

/* OS specific functions */
void os_setup_early_signal_handling(void);
int os_parse_cmd_args(int index, const char *optarg);

void page_size_init(void);

#endif
