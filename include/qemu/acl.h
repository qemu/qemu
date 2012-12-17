/*
 * QEMU access control list management
 *
 * Copyright (C) 2009 Red Hat, Inc
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef __QEMU_ACL_H__
#define __QEMU_ACL_H__

#include "qemu/queue.h"

typedef struct qemu_acl_entry qemu_acl_entry;
typedef struct qemu_acl qemu_acl;

struct qemu_acl_entry {
    char *match;
    int deny;

    QTAILQ_ENTRY(qemu_acl_entry) next;
};

struct qemu_acl {
    char *aclname;
    unsigned int nentries;
    QTAILQ_HEAD(,qemu_acl_entry) entries;
    int defaultDeny;
};

qemu_acl *qemu_acl_init(const char *aclname);

qemu_acl *qemu_acl_find(const char *aclname);

int qemu_acl_party_is_allowed(qemu_acl *acl,
			      const char *party);

void qemu_acl_reset(qemu_acl *acl);

int qemu_acl_append(qemu_acl *acl,
		    int deny,
		    const char *match);
int qemu_acl_insert(qemu_acl *acl,
		    int deny,
		    const char *match,
		    int index);
int qemu_acl_remove(qemu_acl *acl,
		    const char *match);

#endif /* __QEMU_ACL_H__ */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 8
 * End:
 */
