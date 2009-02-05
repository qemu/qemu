/*
 * PowerPC KVM support
 *
 * Copyright IBM Corp. 2008
 *
 * Authors:
 *  Hollis Blanchard <hollisb@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu-common.h"
#include "qemu-timer.h"
#include "kvm_ppc.h"
#include "device_tree.h"

#define PROC_DEVTREE_PATH "/proc/device-tree"

static QEMUTimer *kvmppc_timer;
static unsigned int kvmppc_timer_rate;

#ifdef HAVE_FDT
int kvmppc_read_host_property(const char *node_path, const char *prop,
                                     void *val, size_t len)
{
    char *path;
    FILE *f;
    int ret = 0;
    int pathlen;

    pathlen = snprintf(NULL, 0, "%s/%s/%s", PROC_DEVTREE_PATH, node_path, prop)
              + 1;
    path = qemu_malloc(pathlen);

    snprintf(path, pathlen, "%s/%s/%s", PROC_DEVTREE_PATH, node_path, prop);

    f = fopen(path, "rb");
    if (f == NULL) {
        ret = errno;
        goto free;
    }

    len = fread(val, len, 1, f);
    if (len != 1) {
        ret = ferror(f);
        goto close;
    }

close:
    fclose(f);
free:
    free(path);
out:
    return ret;
}

static int kvmppc_copy_host_cell(void *fdt, const char *node, const char *prop)
{
    uint32_t cell;
    int ret;

    ret = kvmppc_read_host_property(node, prop, &cell, sizeof(cell));
    if (ret < 0) {
        fprintf(stderr, "couldn't read host %s/%s\n", node, prop);
        goto out;
    }

    ret = qemu_devtree_setprop_cell(fdt, node, prop, cell);
    if (ret < 0) {
        fprintf(stderr, "couldn't set guest %s/%s\n", node, prop);
        goto out;
    }

out:
    return ret;
}

void kvmppc_fdt_update(void *fdt)
{
    /* Copy data from the host device tree into the guest. Since the guest can
     * directly access the timebase without host involvement, we must expose
     * the correct frequencies. */
    kvmppc_copy_host_cell(fdt, "/cpus/cpu@0", "clock-frequency");
    kvmppc_copy_host_cell(fdt, "/cpus/cpu@0", "timebase-frequency");
}
#endif

static void kvmppc_timer_hack(void *opaque)
{
    qemu_service_io();
    qemu_mod_timer(kvmppc_timer, qemu_get_clock(vm_clock) + kvmppc_timer_rate);
}

void kvmppc_init(void)
{
    /* XXX The only reason KVM yields control back to qemu is device IO. Since
     * an idle guest does no IO, qemu's device model will never get a chance to
     * run. So, until Qemu gains IO threads, we create this timer to ensure
     * that the device model gets a chance to run. */
    kvmppc_timer_rate = ticks_per_sec / 10;
    kvmppc_timer = qemu_new_timer(vm_clock, &kvmppc_timer_hack, NULL);
    qemu_mod_timer(kvmppc_timer, qemu_get_clock(vm_clock) + kvmppc_timer_rate);
}

