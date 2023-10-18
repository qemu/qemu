#ifndef RAMFB_H
#define RAMFB_H

#include "migration/vmstate.h"

/* ramfb.c */
typedef struct RAMFBState RAMFBState;
void ramfb_display_update(QemuConsole *con, RAMFBState *s);
RAMFBState *ramfb_setup(Error **errp);

extern const VMStateDescription ramfb_vmstate;

/* ramfb-standalone.c */
#define TYPE_RAMFB_DEVICE "ramfb"

#endif /* RAMFB_H */
