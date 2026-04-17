/*
 * QEMU MSHV support
 *
 * Copyright Microsoft, Corp. 2025
 *
 * Authors: Ziqiao Zhou <ziqiaozhou@microsoft.com>
 *          Magnus Kulke <magnuskulke@microsoft.com>
 *          Stanislav Kinsburskii <skinsburskii@microsoft.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "linux/mshv.h"
#include "qemu/error-report.h"
#include "hw/hyperv/hvhdk_mini.h"
#include "hw/hyperv/hvgdk_mini.h"
#include "hw/intc/ioapic.h"
#include "hw/pci/msi.h"
#include "system/mshv.h"
#include "system/mshv_int.h"
#include "trace.h"
#include <sys/ioctl.h>

#define MSHV_IRQFD_RESAMPLE_FLAG (1 << MSHV_IRQFD_BIT_RESAMPLE)
#define MSHV_IRQFD_BIT_DEASSIGN_FLAG (1 << MSHV_IRQFD_BIT_DEASSIGN)

static MshvMsiControl *msi_control;
static QemuMutex msi_control_mutex;

void mshv_init_msicontrol(void)
{
    qemu_mutex_init(&msi_control_mutex);
    msi_control = g_new0(MshvMsiControl, 1);
    msi_control->gsi_routes = g_hash_table_new(g_direct_hash, g_direct_equal);
    msi_control->updated = false;
}

static int add_msi_routing(uint64_t addr, uint32_t data)
{
    struct mshv_user_irq_entry *route_entry;
    uint32_t high_addr = addr >> 32;
    uint32_t low_addr = addr & 0xFFFFFFFF;
    int gsi;
    GHashTable *gsi_routes;

    trace_mshv_add_msi_routing(addr, data);

    assert(msi_control);

    WITH_QEMU_LOCK_GUARD(&msi_control_mutex) {
        /* find an empty slot */
        gsi = 0;
        gsi_routes = msi_control->gsi_routes;
        while (gsi < MSHV_MAX_MSI_ROUTES) {
            route_entry = g_hash_table_lookup(gsi_routes, GINT_TO_POINTER(gsi));
            if (!route_entry) {
                break;
            }
            gsi++;
        }
        if (gsi >= MSHV_MAX_MSI_ROUTES) {
            error_report("No empty gsi slot available");
            return -1;
        }

        /* create new entry */
        route_entry = g_new0(struct mshv_user_irq_entry, 1);
        route_entry->gsi = gsi;
        route_entry->address_hi = high_addr;
        route_entry->address_lo = low_addr;
        route_entry->data = data;

        g_hash_table_insert(gsi_routes, GINT_TO_POINTER(gsi), route_entry);
        msi_control->updated = true;
    }

    return gsi;
}

static int commit_msi_routing_table(int vm_fd)
{
    guint len;
    int i, ret;
    size_t table_size;
    struct mshv_user_irq_table *table;
    GHashTableIter iter;
    gpointer key, value;

    assert(msi_control);

    WITH_QEMU_LOCK_GUARD(&msi_control_mutex) {
        if (!msi_control->updated) {
            /* nothing to update */
            return 0;
        }

        /* Calculate the size of the table */
        len = g_hash_table_size(msi_control->gsi_routes);
        table_size = sizeof(struct mshv_user_irq_table)
                     + len * sizeof(struct mshv_user_irq_entry);
        table = g_malloc0(table_size);

        g_hash_table_iter_init(&iter, msi_control->gsi_routes);
        i = 0;
        while (g_hash_table_iter_next(&iter, &key, &value)) {
            struct mshv_user_irq_entry *entry = value;
            table->entries[i] = *entry;
            i++;
        }
        table->nr = i;

        trace_mshv_commit_msi_routing_table(vm_fd, len);

        ret = ioctl(vm_fd, MSHV_SET_MSI_ROUTING, table);
        g_free(table);
        if (ret < 0) {
            error_report("Failed to commit msi routing table");
            return -1;
        }
        msi_control->updated = false;
    }
    return 0;
}

static int remove_msi_routing(uint32_t gsi)
{
    struct mshv_user_irq_entry *route_entry;
    GHashTable *gsi_routes;

    trace_mshv_remove_msi_routing(gsi);

    if (gsi >= MSHV_MAX_MSI_ROUTES) {
        error_report("Invalid GSI: %u", gsi);
        return -1;
    }

    assert(msi_control);

    WITH_QEMU_LOCK_GUARD(&msi_control_mutex) {
        gsi_routes = msi_control->gsi_routes;
        route_entry = g_hash_table_lookup(gsi_routes, GINT_TO_POINTER(gsi));
        if (route_entry) {
            g_hash_table_remove(gsi_routes, GINT_TO_POINTER(gsi));
            g_free(route_entry);
            msi_control->updated = true;
        }
    }

    return 0;
}

/* Pass an eventfd which is to be used for injecting interrupts from userland */
static int irqfd(int vm_fd, int fd, int resample_fd, uint32_t gsi,
                 uint32_t flags)
{
    int ret;
    struct mshv_user_irqfd arg = {
        .fd = fd,
        .resamplefd = resample_fd,
        .gsi = gsi,
        .flags = flags,
    };

    ret = ioctl(vm_fd, MSHV_IRQFD, &arg);
    if (ret < 0) {
        error_report("Failed to set irqfd: gsi=%u, fd=%d", gsi, fd);
        return -1;
    }
    return ret;
}

static int register_irqfd(int vm_fd, int event_fd, uint32_t gsi)
{
    int ret;

    trace_mshv_register_irqfd(vm_fd, event_fd, gsi);

    ret = irqfd(vm_fd, event_fd, 0, gsi, 0);
    if (ret < 0) {
        error_report("Failed to register irqfd: gsi=%u", gsi);
        return -1;
    }
    return 0;
}

static int register_irqfd_with_resample(int vm_fd, int event_fd,
                                        int resample_fd, uint32_t gsi)
{
    int ret;
    uint32_t flags = MSHV_IRQFD_RESAMPLE_FLAG;

    ret = irqfd(vm_fd, event_fd, resample_fd, gsi, flags);
    if (ret < 0) {
        error_report("Failed to register irqfd with resample: gsi=%u", gsi);
        return -errno;
    }
    return 0;
}

static int unregister_irqfd(int vm_fd, int event_fd, uint32_t gsi)
{
    int ret;
    uint32_t flags = MSHV_IRQFD_BIT_DEASSIGN_FLAG;

    ret = irqfd(vm_fd, event_fd, 0, gsi, flags);
    if (ret < 0) {
        error_report("Failed to unregister irqfd: gsi=%u", gsi);
        return -errno;
    }
    return 0;
}

static int irqchip_update_irqfd_notifier_gsi(const EventNotifier *event,
                                             const EventNotifier *resample,
                                             int virq, bool add)
{
    int fd = event_notifier_get_fd(event);
    int rfd = resample ? event_notifier_get_fd(resample) : -1;
    int vm_fd = mshv_state->vm;

    trace_mshv_irqchip_update_irqfd_notifier_gsi(fd, rfd, virq, add);

    if (!add) {
        return unregister_irqfd(vm_fd, fd, virq);
    }

    if (rfd > 0) {
        return register_irqfd_with_resample(vm_fd, fd, rfd, virq);
    }

    return register_irqfd(vm_fd, fd, virq);
}

static int irqchip_allocate_gsi(MshvState *s, int *gsi)
{
    int next_gsi;

    /* Return the lowest unused GSI in the bitmap */
    next_gsi = find_first_zero_bit(s->used_gsi_bitmap, s->gsi_count);
    if (next_gsi >= s->gsi_count) {
        return -ENOSPC;
    }

    *gsi = next_gsi;

    return 0;
}

static void irqchip_release_gsi(MshvState *s, int gsi)
{
    clear_bit(gsi, s->used_gsi_bitmap);
}

static void add_routing_entry(MshvState *s, struct mshv_user_irq_entry *entry)
{
    struct mshv_user_irq_entry *new;
    int n, size;

    if (s->irq_routes->nr == s->nr_allocated_irq_routes) {
        n = s->nr_allocated_irq_routes * 2;
        if (n < MSHV_MIN_ALLOCATED_MSI_ROUTES) {
            n = MSHV_MIN_ALLOCATED_MSI_ROUTES;
        }
        size = sizeof(struct mshv_user_irq_table);
        size += n * sizeof(*new);
        s->irq_routes = g_realloc(s->irq_routes, size);
        s->nr_allocated_irq_routes = n;
    }

    n = s->irq_routes->nr;
    s->irq_routes->nr++;
    new = &s->irq_routes->entries[n];

    *new = *entry;

    set_bit(entry->gsi, s->used_gsi_bitmap);

    trace_mshv_add_msi_routing(entry->address_lo | entry->address_hi,
                               entry->data);
}

int mshv_irqchip_add_msi_route(AccelRouteChange *c, int vector, PCIDevice *dev)
{
    struct mshv_user_irq_entry entry = { 0 };
    MSIMessage msg = { 0 };
    uint32_t data, high_addr, low_addr;
    int gsi, ret;
    MshvState *s = MSHV_STATE(c->accel);

    if (!pci_available || !dev) {
        return 0;
    }

    msg = pci_get_msi_message(dev, vector);

    ret = irqchip_allocate_gsi(mshv_state, &gsi);
    if (ret < 0) {
        error_report("Could not allocate GSI for MSI route");
        return -1;
    }
    high_addr = msg.address >> 32;
    low_addr = msg.address & 0xFFFFFFFF;
    data = le32_to_cpu(msg.data);

    entry.gsi = gsi;
    entry.address_hi = high_addr;
    entry.address_lo = low_addr;
    entry.data = data;

    if (s->irq_routes->nr < s->gsi_count) {
        add_routing_entry(s, &entry);
        c->changes++;
    } else {
        irqchip_release_gsi(s, gsi);
        return -ENOSPC;
    }

    return gsi;
}

void mshv_irqchip_release_virq(int virq)
{
    remove_msi_routing(virq);
}

static int update_routing_entry(MshvState *s,
                                struct mshv_user_irq_entry *new_entry)
{
    struct mshv_user_irq_entry *entry;
    int n;

    for (n = 0; n < s->irq_routes->nr; n++) {
        entry = &s->irq_routes->entries[n];
        if (entry->gsi != new_entry->gsi) {
            continue;
        }

        if (!memcmp(entry, new_entry, sizeof *entry)) {
            return 0;
        }

        *entry = *new_entry;

        return 0;
    }

    return -ESRCH;
}

int mshv_irqchip_update_msi_route(int virq, MSIMessage msg, PCIDevice *dev)
{
    uint32_t addr_hi = msg.address >> 32;
    uint32_t addr_lo = msg.address & 0xFFFFFFFF;
    uint32_t data = le32_to_cpu(msg.data);
    struct mshv_user_irq_entry entry = {
        .gsi = virq,
        .address_hi = addr_hi,
        .address_lo = addr_lo,
        .data = data,
    };
    int ret;

    ret = update_routing_entry(mshv_state, &entry);
    if (ret < 0) {
        error_report("Failed to set msi routing for gsi %d", virq);
        abort();
    }

    trace_mshv_set_msi_routing(virq, msg.address, data);

    return 0;
}

int mshv_request_interrupt(MshvState *mshv_state, uint32_t interrupt_type, uint32_t vector,
                           uint32_t vp_index, bool logical_dest_mode,
                           bool level_triggered)
{
    int ret;
    int vm_fd = mshv_state->vm;

    if (vector == 0) {
        warn_report("Ignoring request for interrupt vector 0");
        return 0;
    }

    union hv_interrupt_control control = {
        .interrupt_type = interrupt_type,
        .level_triggered = level_triggered,
        .logical_dest_mode = logical_dest_mode,
        .rsvd = 0,
    };

    struct hv_input_assert_virtual_interrupt arg = {0};
    arg.control = control;
    arg.dest_addr = (uint64_t)vp_index;
    arg.vector = vector;

    struct mshv_root_hvcall args = {0};
    args.code   = HVCALL_ASSERT_VIRTUAL_INTERRUPT;
    args.in_sz  = sizeof(arg);
    args.in_ptr = (uint64_t)&arg;

    ret = mshv_hvcall(vm_fd, &args);
    if (ret < 0) {
        error_report("Failed to request interrupt");
        return -errno;
    }
    return 0;
}

void mshv_irqchip_commit_routes(void)
{
    int ret;
    int vm_fd = mshv_state->vm;

    ret = commit_msi_routing_table(vm_fd);
    if (ret < 0) {
        error_report("Failed to commit msi routing table");
        abort();
    }
}

int mshv_irqchip_add_irqfd_notifier_gsi(const EventNotifier *event,
                                        const EventNotifier *resample,
                                        int virq)
{
    return irqchip_update_irqfd_notifier_gsi(event, resample, virq, true);
}

int mshv_irqchip_remove_irqfd_notifier_gsi(const EventNotifier *event,
                                           int virq)
{
    return irqchip_update_irqfd_notifier_gsi(event, NULL, virq, false);
}

int mshv_reserve_ioapic_msi_routes(int vm_fd)
{
    int ret, gsi;

    /*
     * Reserve GSI 0-23 for IOAPIC pins, to avoid conflicts of legacy
     * peripherals with MSI-X devices
     */
    for (gsi = 0; gsi < IOAPIC_NUM_PINS; gsi++) {
        ret = add_msi_routing(0, 0);
        if (ret < 0) {
            error_report("Failed to reserve GSI %d", gsi);
            return -1;
        }
    }

    ret = commit_msi_routing_table(vm_fd);
    if (ret < 0) {
        error_report("Failed to commit reserved IOAPIC MSI routes");
        return -1;
    }

    return 0;
}

void mshv_init_irq_routing(MshvState *s)
{
    int gsi_count = MSHV_MAX_MSI_ROUTES;

    s->irq_routes = g_malloc0(sizeof(*s->irq_routes));
    s->nr_allocated_irq_routes = 0;
    s->gsi_count = gsi_count;
    s->used_gsi_bitmap = bitmap_new(gsi_count);
}
