/*
 * Host xattr.h abstraction
 *
 * Copyright 2011 Red Hat Inc. and/or its affiliates
 *
 * Authors:
 *  Avi Kivity <avi@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2, or any
 * later version.  See the COPYING file in the top-level directory.
 *
 */
#ifndef QEMU_XATTR_H
#define QEMU_XATTR_H

/*
 * Modern distributions (e.g. Fedora 15), have no libattr.so, place attr.h
 * in /usr/include/sys, and don't have ENOATTR.
 */


#ifdef CONFIG_LIBATTR
#  include <attr/xattr.h>
#else
#  define ENOATTR ENODATA
#  include <sys/xattr.h>
#endif

#endif
