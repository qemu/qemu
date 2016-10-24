/* Declarations for use by hardware emulation.  */
#ifndef QEMU_HW_H
#define QEMU_HW_H

#ifdef CONFIG_USER_ONLY
#error Cannot include hw/hw.h from user emulation
#endif

#include "exec/cpu-common.h"
#include "qom/object.h"
#include "exec/memory.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "qemu/module.h"
#include "sysemu/reset.h"

void QEMU_NORETURN hw_error(const char *fmt, ...) GCC_FMT_ATTR(1, 2);

#endif
