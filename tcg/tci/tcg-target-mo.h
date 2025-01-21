/* SPDX-License-Identifier: MIT */
/*
 * Define target-specific memory model
 * Copyright (c) 2009, 2011 Stefan Weil
 */

#ifndef TCG_TARGET_MO_H
#define TCG_TARGET_MO_H

/*
 * We could notice __i386__ or __s390x__ and reduce the barriers depending
 * on the host.  But if you want performance, you use the normal backend.
 * We prefer consistency across hosts on this.
 */
#define TCG_TARGET_DEFAULT_MO  0

#endif
