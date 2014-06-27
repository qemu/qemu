/* Declarations for use by hardware emulation.  */
#ifndef QEMU_HW_H
#define QEMU_HW_H


#ifdef CONFIG_USER_ONLY
#error Cannot include hw/hw.h from user emulation
#endif

#include "exec/cpu-common.h"
#include "exec/ioport.h"
#include "hw/irq.h"
#include "block/aio.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"

typedef void QEMUResetHandler(void *opaque);

void qemu_register_reset(QEMUResetHandler *func, void *opaque);
void qemu_unregister_reset(QEMUResetHandler *func, void *opaque);

void QEMU_NORETURN hw_error(const char *fmt, ...) GCC_FMT_ATTR(1, 2);

#endif
