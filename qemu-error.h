#ifndef QEMU_ERROR_H
#define QEMU_ERROR_H

void qemu_error(const char *fmt, ...) __attribute__ ((format(printf, 1, 2)));
void qemu_error_internal(const char *file, int linenr, const char *func,
                         const char *fmt, ...)
                         __attribute__ ((format(printf, 4, 5)));

#define qemu_error_new(fmt, ...) \
    qemu_error_internal(__FILE__, __LINE__, __func__, fmt, ## __VA_ARGS__)

#endif
