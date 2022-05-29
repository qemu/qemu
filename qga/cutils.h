#ifndef CUTILS_H_
#define CUTILS_H_

#include "qemu/osdep.h"

int qga_open_cloexec(const char *name, int flags, mode_t mode);

#endif /* CUTILS_H_ */
