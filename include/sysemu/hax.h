/*
 * QEMU HAXM support
 *
 * Copyright IBM, Corp. 2008
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * Copyright (c) 2011 Intel Corporation
 *  Written by:
 *  Jiang Yunhong<yunhong.jiang@intel.com>
 *  Xin Xiaohui<xiaohui.xin@intel.com>
 *  Zhang Xiantao<xiantao.zhang@intel.com>
 *
 * Copyright 2016 Google, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

/* header to be included in non-HAX-specific code */

#ifndef QEMU_HAX_H
#define QEMU_HAX_H

int hax_sync_vcpus(void);

#ifdef NEED_CPU_H
# ifdef CONFIG_HAX
#  define CONFIG_HAX_IS_POSSIBLE
# endif
#else /* !NEED_CPU_H */
# define CONFIG_HAX_IS_POSSIBLE
#endif

#ifdef CONFIG_HAX_IS_POSSIBLE

extern bool hax_allowed;

#define hax_enabled()               (hax_allowed)

#else /* !CONFIG_HAX_IS_POSSIBLE */

#define hax_enabled()               (0)

#endif /* CONFIG_HAX_IS_POSSIBLE */

#endif /* QEMU_HAX_H */
