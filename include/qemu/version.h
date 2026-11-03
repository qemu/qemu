/*
 * QEMU version strings
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QEMU_VERSION_H
#define QEMU_VERSION_H

/* qemu-version.h defines QEMU_PKGVERSION only if it is not empty.  */
#include "qemu-version.h"

#ifdef QEMU_PKGVERSION
#define QEMU_FULL_VERSION QEMU_VERSION " (" QEMU_PKGVERSION ")"
#else
#define QEMU_PKGVERSION ""
#define QEMU_FULL_VERSION QEMU_VERSION
#endif

#endif
