#ifndef QEMU_PROGRESS_H
#define QEMU_PROGRESS_H

void qemu_progress_init(int enabled, float min_skip);
void qemu_progress_end(void);
void qemu_progress_print(float delta, int max);

#endif /* QEMU_PROGRESS_H */
