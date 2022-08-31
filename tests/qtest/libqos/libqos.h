#ifndef LIBQOS_H
#define LIBQOS_H

#include "../libqtest.h"
#include "pci.h"
#include "libqos-malloc.h"

typedef struct QOSState QOSState;

typedef struct QOSOps {
    void (*alloc_init)(QGuestAllocator *, QTestState *, QAllocOpts);
    QPCIBus *(*qpci_new)(QTestState *qts, QGuestAllocator *alloc);
    void (*qpci_free)(QPCIBus *bus);
    void (*shutdown)(QOSState *);
} QOSOps;

struct QOSState {
    QTestState *qts;
    QGuestAllocator alloc;
    QPCIBus *pcibus;
    QOSOps *ops;
};

QOSState *qtest_vboot(QOSOps *ops, const char *cmdline_fmt, va_list ap);
QOSState *qtest_boot(QOSOps *ops, const char *cmdline_fmt, ...);
void qtest_common_shutdown(QOSState *qs);
void qtest_shutdown(QOSState *qs);
bool have_qemu_img(void);
void mkimg(const char *file, const char *fmt, unsigned size_mb);
void mkqcow2(const char *file, unsigned size_mb);
void migrate(QOSState *from, QOSState *to, const char *uri);
void prepare_blkdebug_script(const char *debug_fn, const char *event);
void generate_pattern(void *buffer, size_t len, size_t cycle_len);

static inline uint64_t qmalloc(QOSState *q, size_t bytes)
{
    return guest_alloc(&q->alloc, bytes);
}

static inline void qfree(QOSState *q, uint64_t addr)
{
    guest_free(&q->alloc, addr);
}

#endif
