#ifndef QEMU_HW_H
#define QEMU_HW_H

#ifdef CONFIG_USER_ONLY
#error Cannot include hw/hw.h from user emulation
#endif

void QEMU_NORETURN hw_error(const char *fmt, ...) G_GNUC_PRINTF(1, 2);

#endif
