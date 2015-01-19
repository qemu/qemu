#ifndef __libqos_h
#define __libqos_h

#include "libqtest.h"
#include "libqos/pci.h"
#include "libqos/malloc-pc.h"

typedef struct QOSState {
    QTestState *qts;
    QGuestAllocator *alloc;
} QOSState;

QOSState *qtest_boot(const char *cmdline_fmt, ...);
void qtest_shutdown(QOSState *qs);

static inline uint64_t qmalloc(QOSState *q, size_t bytes)
{
    return guest_alloc(q->alloc, bytes);
}

static inline void qfree(QOSState *q, uint64_t addr)
{
    guest_free(q->alloc, addr);
}

#endif
