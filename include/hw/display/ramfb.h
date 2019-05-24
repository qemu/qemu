#ifndef RAMFB_H
#define RAMFB_H

/* ramfb.c */
typedef struct RAMFBState RAMFBState;
void ramfb_display_update(QemuConsole *con, RAMFBState *s);
RAMFBState *ramfb_setup(DeviceState *dev, Error **errp);

/* ramfb-standalone.c */
#define TYPE_RAMFB_DEVICE "ramfb"

#endif /* RAMFB_H */
