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


#include "qemu-common.h"
#include "sysemu.h"
#include "acl.h"

#ifdef HAVE_FNMATCH_H
#include <fnmatch.h>
#endif


static unsigned int nacls = 0;
static qemu_acl **acls = NULL;



qemu_acl *qemu_acl_find(const char *aclname)
{
    int i;
    for (i = 0 ; i < nacls ; i++) {
	if (strcmp(acls[i]->aclname, aclname) == 0)
	    return acls[i];
    }

    return NULL;
}

qemu_acl *qemu_acl_init(const char *aclname)
{
    qemu_acl *acl;

    acl = qemu_acl_find(aclname);
    if (acl)
	return acl;

    acl = qemu_malloc(sizeof(*acl));
    acl->aclname = qemu_strdup(aclname);
    /* Deny by default, so there is no window of "open
     * access" between QEMU starting, and the user setting
     * up ACLs in the monitor */
    acl->defaultDeny = 1;

    acl->nentries = 0;
    TAILQ_INIT(&acl->entries);

    acls = qemu_realloc(acls, sizeof(*acls) * (nacls +1));
    acls[nacls] = acl;
    nacls++;

    return acl;
}

int qemu_acl_party_is_allowed(qemu_acl *acl,
			      const char *party)
{
    qemu_acl_entry *entry;

    TAILQ_FOREACH(entry, &acl->entries, next) {
#ifdef HAVE_FNMATCH_H
	if (fnmatch(entry->match, party, 0) == 0)
	    return entry->deny ? 0 : 1;
#else
	/* No fnmatch, so fallback to exact string matching
	 * instead of allowing wildcards */
	if (strcmp(entry->match, party) == 0)
	    return entry->deny ? 0 : 1;
#endif
    }

    return acl->defaultDeny ? 0 : 1;
}


void qemu_acl_reset(qemu_acl *acl)
{
    qemu_acl_entry *entry;

    /* Put back to deny by default, so there is no window
     * of "open access" while the user re-initializes the
     * access control list */
    acl->defaultDeny = 1;
    TAILQ_FOREACH(entry, &acl->entries, next) {
	TAILQ_REMOVE(&acl->entries, entry, next);
	free(entry->match);
	free(entry);
    }
    acl->nentries = 0;
}


int qemu_acl_append(qemu_acl *acl,
		    int deny,
		    const char *match)
{
    qemu_acl_entry *entry;

    entry = qemu_malloc(sizeof(*entry));
    entry->match = qemu_strdup(match);
    entry->deny = deny;

    TAILQ_INSERT_TAIL(&acl->entries, entry, next);
    acl->nentries++;

    return acl->nentries;
}


int qemu_acl_insert(qemu_acl *acl,
		    int deny,
		    const char *match,
		    int index)
{
    qemu_acl_entry *entry;
    qemu_acl_entry *tmp;
    int i = 0;

    if (index <= 0)
	return -1;
    if (index >= acl->nentries)
	return qemu_acl_append(acl, deny, match);


    entry = qemu_malloc(sizeof(*entry));
    entry->match = qemu_strdup(match);
    entry->deny = deny;

    TAILQ_FOREACH(tmp, &acl->entries, next) {
	i++;
	if (i == index) {
	    TAILQ_INSERT_BEFORE(tmp, entry, next);
	    acl->nentries++;
	    break;
	}
    }

    return i;
}

int qemu_acl_remove(qemu_acl *acl,
		    const char *match)
{
    qemu_acl_entry *entry;
    int i = 0;

    TAILQ_FOREACH(entry, &acl->entries, next) {
	i++;
	if (strcmp(entry->match, match) == 0) {
	    TAILQ_REMOVE(&acl->entries, entry, next);
	    return i;
	}
    }
    return -1;
}


/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 8
 * End:
 */
