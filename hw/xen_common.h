#ifndef QEMU_HW_XEN_COMMON_H
#define QEMU_HW_XEN_COMMON_H 1

#include <stddef.h>
#include <inttypes.h>

#include <xenctrl.h>
#include <xs.h>
#include <xen/io/xenbus.h>

#include "hw.h"
#include "xen.h"
#include "qemu-queue.h"

/*
 * tweaks needed to build with different xen versions
 *  0x00030205 -> 3.1.0
 *  0x00030207 -> 3.2.0
 *  0x00030208 -> unstable
 */
#include <xen/xen-compat.h>
#if __XEN_LATEST_INTERFACE_VERSION__ < 0x00030205
# define evtchn_port_or_error_t int
#endif
#if __XEN_LATEST_INTERFACE_VERSION__ < 0x00030207
# define xc_map_foreign_pages xc_map_foreign_batch
#endif
#if __XEN_LATEST_INTERFACE_VERSION__ < 0x00030208
# define xen_mb()  mb()
# define xen_rmb() rmb()
# define xen_wmb() wmb()
#endif

#endif /* QEMU_HW_XEN_COMMON_H */
