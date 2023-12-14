/*
 * Asynchronous teardown
 *
 * Copyright IBM, Corp. 2022
 *
 * Authors:
 *  Claudio Imbrenda <imbrenda@linux.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at your
 * option) any later version.  See the COPYING file in the top-level directory.
 *
 */
#ifndef QEMU_ASYNC_TEARDOWN_H
#define QEMU_ASYNC_TEARDOWN_H

#ifdef CONFIG_LINUX
void init_async_teardown(void);
#endif

#endif
