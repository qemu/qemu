/*
 * Copyright (C) 2015 Virtual Open Systems SAS
 * Author: Alexander Spyridakis <a.spyridakis@virtualopensystems.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef _MINILIB_H_
#define _MINILIB_H_

/*
 * Provided by the individual arch
 */
extern void __sys_outc(char c);

/*
 * Provided by the common minilib
 */
void ml_printf(const char *fmt, ...);

#endif /* _MINILIB_H_ */
