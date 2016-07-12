#ifndef QEMU_THREAD_WIN32_H
#define QEMU_THREAD_WIN32_H

#include <windows.h>

struct QemuMutex {
    CRITICAL_SECTION lock;
    LONG owner;
};

struct QemuCond {
    LONG waiters, target;
    HANDLE sema;
    HANDLE continue_event;
};

struct QemuSemaphore {
    HANDLE sema;
};

struct QemuEvent {
    int value;
    HANDLE event;
};

typedef struct QemuThreadData QemuThreadData;
struct QemuThread {
    QemuThreadData *data;
    unsigned tid;
};

/* Only valid for joinable threads.  */
HANDLE qemu_thread_get_handle(QemuThread *thread);

#endif
