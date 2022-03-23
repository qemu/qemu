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

#endif
