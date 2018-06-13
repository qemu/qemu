#ifndef RAMFB_H
#define RAMFB_H

/* ramfb.c */
typedef struct RAMFBState RAMFBState;
void ramfb_display_update(QemuConsole *con, RAMFBState *s);
RAMFBState *ramfb_setup(Error **errp);

#endif /* RAMFB_H */
