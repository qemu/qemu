/*
 * Generic Balloon handlers and management
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (C) 2011 Red Hat, Inc.
 * Copyright (C) 2011 Amit Shah <amit.shah@redhat.com>
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

#include "qemu/osdep.h"
#include "qemu/atomic.h"
#include "sysemu/kvm.h"
#include "sysemu/balloon.h"
#include "trace-root.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-misc.h"
#include "qapi/qmp/qerror.h"

static QEMUBalloonEvent *balloon_event_fn;
static QEMUBalloonStatus *balloon_stat_fn;
static void *balloon_opaque;
static int balloon_inhibit_count;

bool qemu_balloon_is_inhibited(void)
{
    return atomic_read(&balloon_inhibit_count) > 0;
}

void qemu_balloon_inhibit(bool state)
{
    if (state) {
        atomic_inc(&balloon_inhibit_count);
    } else {
        atomic_dec(&balloon_inhibit_count);
    }

    assert(atomic_read(&balloon_inhibit_count) >= 0);
}

static bool have_balloon(Error **errp)
{
    if (kvm_enabled() && !kvm_has_sync_mmu()) {
        error_set(errp, ERROR_CLASS_KVM_MISSING_CAP,
                  "Using KVM without synchronous MMU, balloon unavailable");
        return false;
    }
    if (!balloon_event_fn) {
        error_set(errp, ERROR_CLASS_DEVICE_NOT_ACTIVE,
                  "No balloon device has been activated");
        return false;
    }
    return true;
}

int qemu_add_balloon_handler(QEMUBalloonEvent *event_func,
                             QEMUBalloonStatus *stat_func, void *opaque)
{
    if (balloon_event_fn || balloon_stat_fn || balloon_opaque) {
        /* We're already registered one balloon handler.  How many can
         * a guest really have?
         */
        return -1;
    }
    balloon_event_fn = event_func;
    balloon_stat_fn = stat_func;
    balloon_opaque = opaque;
    return 0;
}

void qemu_remove_balloon_handler(void *opaque)
{
    if (balloon_opaque != opaque) {
        return;
    }
    balloon_event_fn = NULL;
    balloon_stat_fn = NULL;
    balloon_opaque = NULL;
}

BalloonInfo *qmp_query_balloon(Error **errp)
{
    BalloonInfo *info;

    if (!have_balloon(errp)) {
        return NULL;
    }

    info = g_malloc0(sizeof(*info));
    balloon_stat_fn(balloon_opaque, info);
    return info;
}

void qmp_balloon(int64_t target, Error **errp)
{
    if (!have_balloon(errp)) {
        return;
    }

    if (target <= 0) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "target", "a size");
        return;
    }

    trace_balloon_event(balloon_opaque, target);
    balloon_event_fn(balloon_opaque, target);
}
