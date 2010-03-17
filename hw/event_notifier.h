#ifndef QEMU_EVENT_NOTIFIER_H
#define QEMU_EVENT_NOTIFIER_H

#include "qemu-common.h"

struct EventNotifier {
	int fd;
};

int event_notifier_init(EventNotifier *, int active);
void event_notifier_cleanup(EventNotifier *);
int event_notifier_get_fd(EventNotifier *);
int event_notifier_test_and_clear(EventNotifier *);
int event_notifier_test(EventNotifier *);

#endif
