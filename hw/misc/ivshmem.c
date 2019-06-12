/*
 * Inter-VM Shared Memory PCI device.
 *
 * Author:
 *      Cam Macdonell <cam@cs.ualberta.ca>
 *
 * Based On: cirrus_vga.c
 *          Copyright (c) 2004 Fabrice Bellard
 *          Copyright (c) 2004 Makoto Suzuki (suzu)
 *
 *      and rtl8139.c
 *          Copyright (c) 2006 Igor Kovalenko
 *
 * This code is licensed under the GNU GPL v2.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "qemu/cutils.h"
#include "hw/hw.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "sysemu/kvm.h"
#include "migration/blocker.h"
#include "qemu/error-report.h"
#include "qemu/event_notifier.h"
#include "qemu/module.h"
#include "qom/object_interfaces.h"
#include "chardev/char-fe.h"
#include "sysemu/hostmem.h"
#include "sysemu/qtest.h"
#include "qapi/visitor.h"

#include "hw/misc/ivshmem.h"

#define PCI_VENDOR_ID_IVSHMEM   PCI_VENDOR_ID_REDHAT_QUMRANET
#define PCI_DEVICE_ID_IVSHMEM   0x1110

#define IVSHMEM_MAX_PEERS UINT16_MAX
#define IVSHMEM_IOEVENTFD   0
#define IVSHMEM_MSI     1

#define IVSHMEM_REG_BAR_SIZE 0x100

#define IVSHMEM_DEBUG 0
#define IVSHMEM_DPRINTF(fmt, ...)                       \
    do {                                                \
        if (IVSHMEM_DEBUG) {                            \
            printf("IVSHMEM: " fmt, ## __VA_ARGS__);    \
        }                                               \
    } while (0)

#define TYPE_IVSHMEM_COMMON "ivshmem-common"
#define IVSHMEM_COMMON(obj) \
    OBJECT_CHECK(IVShmemState, (obj), TYPE_IVSHMEM_COMMON)

#define TYPE_IVSHMEM_PLAIN "ivshmem-plain"
#define IVSHMEM_PLAIN(obj) \
    OBJECT_CHECK(IVShmemState, (obj), TYPE_IVSHMEM_PLAIN)

#define TYPE_IVSHMEM_DOORBELL "ivshmem-doorbell"
#define IVSHMEM_DOORBELL(obj) \
    OBJECT_CHECK(IVShmemState, (obj), TYPE_IVSHMEM_DOORBELL)

#define TYPE_IVSHMEM "ivshmem"
#define IVSHMEM(obj) \
    OBJECT_CHECK(IVShmemState, (obj), TYPE_IVSHMEM)

typedef struct Peer {
    int nb_eventfds;
    EventNotifier *eventfds;
} Peer;

typedef struct MSIVector {
    PCIDevice *pdev;
    int virq;
    bool unmasked;
} MSIVector;

typedef struct IVShmemState {
    /*< private >*/
    PCIDevice parent_obj;
    /*< public >*/

    uint32_t features;

    /* exactly one of these two may be set */
    HostMemoryBackend *hostmem; /* with interrupts */
    CharBackend server_chr; /* without interrupts */

    /* registers */
    uint32_t intrmask;
    uint32_t intrstatus;
    int vm_id;

    /* BARs */
    MemoryRegion ivshmem_mmio;  /* BAR 0 (registers) */
    MemoryRegion *ivshmem_bar2; /* BAR 2 (shared memory) */
    MemoryRegion server_bar2;   /* used with server_chr */

    /* interrupt support */
    Peer *peers;
    int nb_peers;               /* space in @peers[] */
    uint32_t vectors;
    MSIVector *msi_vectors;
    uint64_t msg_buf;           /* buffer for receiving server messages */
    int msg_buffered_bytes;     /* #bytes in @msg_buf */

    /* migration stuff */
    OnOffAuto master;
    Error *migration_blocker;
} IVShmemState;

/* registers for the Inter-VM shared memory device */
enum ivshmem_registers {
    INTRMASK = 0,
    INTRSTATUS = 4,
    IVPOSITION = 8,
    DOORBELL = 12,
};

static inline uint32_t ivshmem_has_feature(IVShmemState *ivs,
                                                    unsigned int feature) {
    return (ivs->features & (1 << feature));
}

static inline bool ivshmem_is_master(IVShmemState *s)
{
    assert(s->master != ON_OFF_AUTO_AUTO);
    return s->master == ON_OFF_AUTO_ON;
}

static void ivshmem_update_irq(IVShmemState *s)
{
    PCIDevice *d = PCI_DEVICE(s);
    uint32_t isr = s->intrstatus & s->intrmask;

    /*
     * Do nothing unless the device actually uses INTx.  Here's how
     * the device variants signal interrupts, what they put in PCI
     * config space:
     * Device variant    Interrupt  Interrupt Pin  MSI-X cap.
     * ivshmem-plain         none            0         no
     * ivshmem-doorbell     MSI-X            1        yes(1)
     * ivshmem,msi=off       INTx            1         no
     * ivshmem,msi=on       MSI-X            1(2)     yes(1)
     * (1) if guest enabled MSI-X
     * (2) the device lies
     * Leads to the condition for doing nothing:
     */
    if (ivshmem_has_feature(s, IVSHMEM_MSI)
        || !d->config[PCI_INTERRUPT_PIN]) {
        return;
    }

    /* don't print ISR resets */
    if (isr) {
        IVSHMEM_DPRINTF("Set IRQ to %d (%04x %04x)\n",
                        isr ? 1 : 0, s->intrstatus, s->intrmask);
    }

    pci_set_irq(d, isr != 0);
}

static void ivshmem_IntrMask_write(IVShmemState *s, uint32_t val)
{
    IVSHMEM_DPRINTF("IntrMask write(w) val = 0x%04x\n", val);

    s->intrmask = val;
    ivshmem_update_irq(s);
}

static uint32_t ivshmem_IntrMask_read(IVShmemState *s)
{
    uint32_t ret = s->intrmask;

    IVSHMEM_DPRINTF("intrmask read(w) val = 0x%04x\n", ret);
    return ret;
}

static void ivshmem_IntrStatus_write(IVShmemState *s, uint32_t val)
{
    IVSHMEM_DPRINTF("IntrStatus write(w) val = 0x%04x\n", val);

    s->intrstatus = val;
    ivshmem_update_irq(s);
}

static uint32_t ivshmem_IntrStatus_read(IVShmemState *s)
{
    uint32_t ret = s->intrstatus;

    /* reading ISR clears all interrupts */
    s->intrstatus = 0;
    ivshmem_update_irq(s);
    return ret;
}

static void ivshmem_io_write(void *opaque, hwaddr addr,
                             uint64_t val, unsigned size)
{
    IVShmemState *s = opaque;

    uint16_t dest = val >> 16;
    uint16_t vector = val & 0xff;

    addr &= 0xfc;

    IVSHMEM_DPRINTF("writing to addr " TARGET_FMT_plx "\n", addr);
    switch (addr)
    {
        case INTRMASK:
            ivshmem_IntrMask_write(s, val);
            break;

        case INTRSTATUS:
            ivshmem_IntrStatus_write(s, val);
            break;

        case DOORBELL:
            /* check that dest VM ID is reasonable */
            if (dest >= s->nb_peers) {
                IVSHMEM_DPRINTF("Invalid destination VM ID (%d)\n", dest);
                break;
            }

            /* check doorbell range */
            if (vector < s->peers[dest].nb_eventfds) {
                IVSHMEM_DPRINTF("Notifying VM %d on vector %d\n", dest, vector);
                event_notifier_set(&s->peers[dest].eventfds[vector]);
            } else {
                IVSHMEM_DPRINTF("Invalid destination vector %d on VM %d\n",
                                vector, dest);
            }
            break;
        default:
            IVSHMEM_DPRINTF("Unhandled write " TARGET_FMT_plx "\n", addr);
    }
}

static uint64_t ivshmem_io_read(void *opaque, hwaddr addr,
                                unsigned size)
{

    IVShmemState *s = opaque;
    uint32_t ret;

    switch (addr)
    {
        case INTRMASK:
            ret = ivshmem_IntrMask_read(s);
            break;

        case INTRSTATUS:
            ret = ivshmem_IntrStatus_read(s);
            break;

        case IVPOSITION:
            ret = s->vm_id;
            break;

        default:
            IVSHMEM_DPRINTF("why are we reading " TARGET_FMT_plx "\n", addr);
            ret = 0;
    }

    return ret;
}

static const MemoryRegionOps ivshmem_mmio_ops = {
    .read = ivshmem_io_read,
    .write = ivshmem_io_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void ivshmem_vector_notify(void *opaque)
{
    MSIVector *entry = opaque;
    PCIDevice *pdev = entry->pdev;
    IVShmemState *s = IVSHMEM_COMMON(pdev);
    int vector = entry - s->msi_vectors;
    EventNotifier *n = &s->peers[s->vm_id].eventfds[vector];

    if (!event_notifier_test_and_clear(n)) {
        return;
    }

    IVSHMEM_DPRINTF("interrupt on vector %p %d\n", pdev, vector);
    if (ivshmem_has_feature(s, IVSHMEM_MSI)) {
        if (msix_enabled(pdev)) {
            msix_notify(pdev, vector);
        }
    } else {
        ivshmem_IntrStatus_write(s, 1);
    }
}

static int ivshmem_vector_unmask(PCIDevice *dev, unsigned vector,
                                 MSIMessage msg)
{
    IVShmemState *s = IVSHMEM_COMMON(dev);
    EventNotifier *n = &s->peers[s->vm_id].eventfds[vector];
    MSIVector *v = &s->msi_vectors[vector];
    int ret;

    IVSHMEM_DPRINTF("vector unmask %p %d\n", dev, vector);
    if (!v->pdev) {
        error_report("ivshmem: vector %d route does not exist", vector);
        return -EINVAL;
    }
    assert(!v->unmasked);

    ret = kvm_irqchip_update_msi_route(kvm_state, v->virq, msg, dev);
    if (ret < 0) {
        return ret;
    }
    kvm_irqchip_commit_routes(kvm_state);

    ret = kvm_irqchip_add_irqfd_notifier_gsi(kvm_state, n, NULL, v->virq);
    if (ret < 0) {
        return ret;
    }
    v->unmasked = true;

    return 0;
}

static void ivshmem_vector_mask(PCIDevice *dev, unsigned vector)
{
    IVShmemState *s = IVSHMEM_COMMON(dev);
    EventNotifier *n = &s->peers[s->vm_id].eventfds[vector];
    MSIVector *v = &s->msi_vectors[vector];
    int ret;

    IVSHMEM_DPRINTF("vector mask %p %d\n", dev, vector);
    if (!v->pdev) {
        error_report("ivshmem: vector %d route does not exist", vector);
        return;
    }
    assert(v->unmasked);

    ret = kvm_irqchip_remove_irqfd_notifier_gsi(kvm_state, n, v->virq);
    if (ret < 0) {
        error_report("remove_irqfd_notifier_gsi failed");
        return;
    }
    v->unmasked = false;
}

static void ivshmem_vector_poll(PCIDevice *dev,
                                unsigned int vector_start,
                                unsigned int vector_end)
{
    IVShmemState *s = IVSHMEM_COMMON(dev);
    unsigned int vector;

    IVSHMEM_DPRINTF("vector poll %p %d-%d\n", dev, vector_start, vector_end);

    vector_end = MIN(vector_end, s->vectors);

    for (vector = vector_start; vector < vector_end; vector++) {
        EventNotifier *notifier = &s->peers[s->vm_id].eventfds[vector];

        if (!msix_is_masked(dev, vector)) {
            continue;
        }

        if (event_notifier_test_and_clear(notifier)) {
            msix_set_pending(dev, vector);
        }
    }
}

static void watch_vector_notifier(IVShmemState *s, EventNotifier *n,
                                 int vector)
{
    int eventfd = event_notifier_get_fd(n);

    assert(!s->msi_vectors[vector].pdev);
    s->msi_vectors[vector].pdev = PCI_DEVICE(s);

    qemu_set_fd_handler(eventfd, ivshmem_vector_notify,
                        NULL, &s->msi_vectors[vector]);
}

static void ivshmem_add_eventfd(IVShmemState *s, int posn, int i)
{
    memory_region_add_eventfd(&s->ivshmem_mmio,
                              DOORBELL,
                              4,
                              true,
                              (posn << 16) | i,
                              &s->peers[posn].eventfds[i]);
}

static void ivshmem_del_eventfd(IVShmemState *s, int posn, int i)
{
    memory_region_del_eventfd(&s->ivshmem_mmio,
                              DOORBELL,
                              4,
                              true,
                              (posn << 16) | i,
                              &s->peers[posn].eventfds[i]);
}

static void close_peer_eventfds(IVShmemState *s, int posn)
{
    int i, n;

    assert(posn >= 0 && posn < s->nb_peers);
    n = s->peers[posn].nb_eventfds;

    if (ivshmem_has_feature(s, IVSHMEM_IOEVENTFD)) {
        memory_region_transaction_begin();
        for (i = 0; i < n; i++) {
            ivshmem_del_eventfd(s, posn, i);
        }
        memory_region_transaction_commit();
    }

    for (i = 0; i < n; i++) {
        event_notifier_cleanup(&s->peers[posn].eventfds[i]);
    }

    g_free(s->peers[posn].eventfds);
    s->peers[posn].nb_eventfds = 0;
}

static void resize_peers(IVShmemState *s, int nb_peers)
{
    int old_nb_peers = s->nb_peers;
    int i;

    assert(nb_peers > old_nb_peers);
    IVSHMEM_DPRINTF("bumping storage to %d peers\n", nb_peers);

    s->peers = g_realloc(s->peers, nb_peers * sizeof(Peer));
    s->nb_peers = nb_peers;

    for (i = old_nb_peers; i < nb_peers; i++) {
        s->peers[i].eventfds = g_new0(EventNotifier, s->vectors);
        s->peers[i].nb_eventfds = 0;
    }
}

static void ivshmem_add_kvm_msi_virq(IVShmemState *s, int vector,
                                     Error **errp)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    int ret;

    IVSHMEM_DPRINTF("ivshmem_add_kvm_msi_virq vector:%d\n", vector);
    assert(!s->msi_vectors[vector].pdev);

    ret = kvm_irqchip_add_msi_route(kvm_state, vector, pdev);
    if (ret < 0) {
        error_setg(errp, "kvm_irqchip_add_msi_route failed");
        return;
    }

    s->msi_vectors[vector].virq = ret;
    s->msi_vectors[vector].pdev = pdev;
}

static void setup_interrupt(IVShmemState *s, int vector, Error **errp)
{
    EventNotifier *n = &s->peers[s->vm_id].eventfds[vector];
    bool with_irqfd = kvm_msi_via_irqfd_enabled() &&
        ivshmem_has_feature(s, IVSHMEM_MSI);
    PCIDevice *pdev = PCI_DEVICE(s);
    Error *err = NULL;

    IVSHMEM_DPRINTF("setting up interrupt for vector: %d\n", vector);

    if (!with_irqfd) {
        IVSHMEM_DPRINTF("with eventfd\n");
        watch_vector_notifier(s, n, vector);
    } else if (msix_enabled(pdev)) {
        IVSHMEM_DPRINTF("with irqfd\n");
        ivshmem_add_kvm_msi_virq(s, vector, &err);
        if (err) {
            error_propagate(errp, err);
            return;
        }

        if (!msix_is_masked(pdev, vector)) {
            kvm_irqchip_add_irqfd_notifier_gsi(kvm_state, n, NULL,
                                               s->msi_vectors[vector].virq);
            /* TODO handle error */
        }
    } else {
        /* it will be delayed until msix is enabled, in write_config */
        IVSHMEM_DPRINTF("with irqfd, delayed until msix enabled\n");
    }
}

static void process_msg_shmem(IVShmemState *s, int fd, Error **errp)
{
    Error *local_err = NULL;
    struct stat buf;
    size_t size;

    if (s->ivshmem_bar2) {
        error_setg(errp, "server sent unexpected shared memory message");
        close(fd);
        return;
    }

    if (fstat(fd, &buf) < 0) {
        error_setg_errno(errp, errno,
            "can't determine size of shared memory sent by server");
        close(fd);
        return;
    }

    size = buf.st_size;

    /* mmap the region and map into the BAR2 */
    memory_region_init_ram_from_fd(&s->server_bar2, OBJECT(s),
                                   "ivshmem.bar2", size, true, fd, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    s->ivshmem_bar2 = &s->server_bar2;
}

static void process_msg_disconnect(IVShmemState *s, uint16_t posn,
                                   Error **errp)
{
    IVSHMEM_DPRINTF("posn %d has gone away\n", posn);
    if (posn >= s->nb_peers || posn == s->vm_id) {
        error_setg(errp, "invalid peer %d", posn);
        return;
    }
    close_peer_eventfds(s, posn);
}

static void process_msg_connect(IVShmemState *s, uint16_t posn, int fd,
                                Error **errp)
{
    Peer *peer = &s->peers[posn];
    int vector;

    /*
     * The N-th connect message for this peer comes with the file
     * descriptor for vector N-1.  Count messages to find the vector.
     */
    if (peer->nb_eventfds >= s->vectors) {
        error_setg(errp, "Too many eventfd received, device has %d vectors",
                   s->vectors);
        close(fd);
        return;
    }
    vector = peer->nb_eventfds++;

    IVSHMEM_DPRINTF("eventfds[%d][%d] = %d\n", posn, vector, fd);
    event_notifier_init_fd(&peer->eventfds[vector], fd);
    fcntl_setfl(fd, O_NONBLOCK); /* msix/irqfd poll non block */

    if (posn == s->vm_id) {
        setup_interrupt(s, vector, errp);
        /* TODO do we need to handle the error? */
    }

    if (ivshmem_has_feature(s, IVSHMEM_IOEVENTFD)) {
        ivshmem_add_eventfd(s, posn, vector);
    }
}

static void process_msg(IVShmemState *s, int64_t msg, int fd, Error **errp)
{
    IVSHMEM_DPRINTF("posn is %" PRId64 ", fd is %d\n", msg, fd);

    if (msg < -1 || msg > IVSHMEM_MAX_PEERS) {
        error_setg(errp, "server sent invalid message %" PRId64, msg);
        close(fd);
        return;
    }

    if (msg == -1) {
        process_msg_shmem(s, fd, errp);
        return;
    }

    if (msg >= s->nb_peers) {
        resize_peers(s, msg + 1);
    }

    if (fd >= 0) {
        process_msg_connect(s, msg, fd, errp);
    } else {
        process_msg_disconnect(s, msg, errp);
    }
}

static int ivshmem_can_receive(void *opaque)
{
    IVShmemState *s = opaque;

    assert(s->msg_buffered_bytes < sizeof(s->msg_buf));
    return sizeof(s->msg_buf) - s->msg_buffered_bytes;
}

static void ivshmem_read(void *opaque, const uint8_t *buf, int size)
{
    IVShmemState *s = opaque;
    Error *err = NULL;
    int fd;
    int64_t msg;

    assert(size >= 0 && s->msg_buffered_bytes + size <= sizeof(s->msg_buf));
    memcpy((unsigned char *)&s->msg_buf + s->msg_buffered_bytes, buf, size);
    s->msg_buffered_bytes += size;
    if (s->msg_buffered_bytes < sizeof(s->msg_buf)) {
        return;
    }
    msg = le64_to_cpu(s->msg_buf);
    s->msg_buffered_bytes = 0;

    fd = qemu_chr_fe_get_msgfd(&s->server_chr);

    process_msg(s, msg, fd, &err);
    if (err) {
        error_report_err(err);
    }
}

static int64_t ivshmem_recv_msg(IVShmemState *s, int *pfd, Error **errp)
{
    int64_t msg;
    int n, ret;

    n = 0;
    do {
        ret = qemu_chr_fe_read_all(&s->server_chr, (uint8_t *)&msg + n,
                                   sizeof(msg) - n);
        if (ret < 0) {
            if (ret == -EINTR) {
                continue;
            }
            error_setg_errno(errp, -ret, "read from server failed");
            return INT64_MIN;
        }
        n += ret;
    } while (n < sizeof(msg));

    *pfd = qemu_chr_fe_get_msgfd(&s->server_chr);
    return le64_to_cpu(msg);
}

static void ivshmem_recv_setup(IVShmemState *s, Error **errp)
{
    Error *err = NULL;
    int64_t msg;
    int fd;

    msg = ivshmem_recv_msg(s, &fd, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    if (msg != IVSHMEM_PROTOCOL_VERSION) {
        error_setg(errp, "server sent version %" PRId64 ", expecting %d",
                   msg, IVSHMEM_PROTOCOL_VERSION);
        return;
    }
    if (fd != -1) {
        error_setg(errp, "server sent invalid version message");
        return;
    }

    /*
     * ivshmem-server sends the remaining initial messages in a fixed
     * order, but the device has always accepted them in any order.
     * Stay as compatible as practical, just in case people use
     * servers that behave differently.
     */

    /*
     * ivshmem_device_spec.txt has always required the ID message
     * right here, and ivshmem-server has always complied.  However,
     * older versions of the device accepted it out of order, but
     * broke when an interrupt setup message arrived before it.
     */
    msg = ivshmem_recv_msg(s, &fd, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    if (fd != -1 || msg < 0 || msg > IVSHMEM_MAX_PEERS) {
        error_setg(errp, "server sent invalid ID message");
        return;
    }
    s->vm_id = msg;

    /*
     * Receive more messages until we got shared memory.
     */
    do {
        msg = ivshmem_recv_msg(s, &fd, &err);
        if (err) {
            error_propagate(errp, err);
            return;
        }
        process_msg(s, msg, fd, &err);
        if (err) {
            error_propagate(errp, err);
            return;
        }
    } while (msg != -1);

    /*
     * This function must either map the shared memory or fail.  The
     * loop above ensures that: it terminates normally only after it
     * successfully processed the server's shared memory message.
     * Assert that actually mapped the shared memory:
     */
    assert(s->ivshmem_bar2);
}

/* Select the MSI-X vectors used by device.
 * ivshmem maps events to vectors statically, so
 * we just enable all vectors on init and after reset. */
static void ivshmem_msix_vector_use(IVShmemState *s)
{
    PCIDevice *d = PCI_DEVICE(s);
    int i;

    for (i = 0; i < s->vectors; i++) {
        msix_vector_use(d, i);
    }
}

static void ivshmem_disable_irqfd(IVShmemState *s);

static void ivshmem_reset(DeviceState *d)
{
    IVShmemState *s = IVSHMEM_COMMON(d);

    ivshmem_disable_irqfd(s);

    s->intrstatus = 0;
    s->intrmask = 0;
    if (ivshmem_has_feature(s, IVSHMEM_MSI)) {
        ivshmem_msix_vector_use(s);
    }
}

static int ivshmem_setup_interrupts(IVShmemState *s, Error **errp)
{
    /* allocate QEMU callback data for receiving interrupts */
    s->msi_vectors = g_malloc0(s->vectors * sizeof(MSIVector));

    if (ivshmem_has_feature(s, IVSHMEM_MSI)) {
        if (msix_init_exclusive_bar(PCI_DEVICE(s), s->vectors, 1, errp)) {
            return -1;
        }

        IVSHMEM_DPRINTF("msix initialized (%d vectors)\n", s->vectors);
        ivshmem_msix_vector_use(s);
    }

    return 0;
}

static void ivshmem_remove_kvm_msi_virq(IVShmemState *s, int vector)
{
    IVSHMEM_DPRINTF("ivshmem_remove_kvm_msi_virq vector:%d\n", vector);

    if (s->msi_vectors[vector].pdev == NULL) {
        return;
    }

    /* it was cleaned when masked in the frontend. */
    kvm_irqchip_release_virq(kvm_state, s->msi_vectors[vector].virq);

    s->msi_vectors[vector].pdev = NULL;
}

static void ivshmem_enable_irqfd(IVShmemState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    int i;

    for (i = 0; i < s->peers[s->vm_id].nb_eventfds; i++) {
        Error *err = NULL;

        ivshmem_add_kvm_msi_virq(s, i, &err);
        if (err) {
            error_report_err(err);
            goto undo;
        }
    }

    if (msix_set_vector_notifiers(pdev,
                                  ivshmem_vector_unmask,
                                  ivshmem_vector_mask,
                                  ivshmem_vector_poll)) {
        error_report("ivshmem: msix_set_vector_notifiers failed");
        goto undo;
    }
    return;

undo:
    while (--i >= 0) {
        ivshmem_remove_kvm_msi_virq(s, i);
    }
}

static void ivshmem_disable_irqfd(IVShmemState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    int i;

    if (!pdev->msix_vector_use_notifier) {
        return;
    }

    msix_unset_vector_notifiers(pdev);

    for (i = 0; i < s->peers[s->vm_id].nb_eventfds; i++) {
        /*
         * MSI-X is already disabled here so msix_unset_vector_notifiers()
         * didn't call our release notifier.  Do it now to keep our masks and
         * unmasks balanced.
         */
        if (s->msi_vectors[i].unmasked) {
            ivshmem_vector_mask(pdev, i);
        }
        ivshmem_remove_kvm_msi_virq(s, i);
    }

}

static void ivshmem_write_config(PCIDevice *pdev, uint32_t address,
                                 uint32_t val, int len)
{
    IVShmemState *s = IVSHMEM_COMMON(pdev);
    int is_enabled, was_enabled = msix_enabled(pdev);

    pci_default_write_config(pdev, address, val, len);
    is_enabled = msix_enabled(pdev);

    if (kvm_msi_via_irqfd_enabled()) {
        if (!was_enabled && is_enabled) {
            ivshmem_enable_irqfd(s);
        } else if (was_enabled && !is_enabled) {
            ivshmem_disable_irqfd(s);
        }
    }
}

static void ivshmem_common_realize(PCIDevice *dev, Error **errp)
{
    IVShmemState *s = IVSHMEM_COMMON(dev);
    Error *err = NULL;
    uint8_t *pci_conf;
    Error *local_err = NULL;

    /* IRQFD requires MSI */
    if (ivshmem_has_feature(s, IVSHMEM_IOEVENTFD) &&
        !ivshmem_has_feature(s, IVSHMEM_MSI)) {
        error_setg(errp, "ioeventfd/irqfd requires MSI");
        return;
    }

    pci_conf = dev->config;
    pci_conf[PCI_COMMAND] = PCI_COMMAND_IO | PCI_COMMAND_MEMORY;

    memory_region_init_io(&s->ivshmem_mmio, OBJECT(s), &ivshmem_mmio_ops, s,
                          "ivshmem-mmio", IVSHMEM_REG_BAR_SIZE);

    /* region for registers*/
    pci_register_bar(dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY,
                     &s->ivshmem_mmio);

    if (s->hostmem != NULL) {
        IVSHMEM_DPRINTF("using hostmem\n");

        s->ivshmem_bar2 = host_memory_backend_get_memory(s->hostmem);
        host_memory_backend_set_mapped(s->hostmem, true);
    } else {
        Chardev *chr = qemu_chr_fe_get_driver(&s->server_chr);
        assert(chr);

        IVSHMEM_DPRINTF("using shared memory server (socket = %s)\n",
                        chr->filename);

        /* we allocate enough space for 16 peers and grow as needed */
        resize_peers(s, 16);

        /*
         * Receive setup messages from server synchronously.
         * Older versions did it asynchronously, but that creates a
         * number of entertaining race conditions.
         */
        ivshmem_recv_setup(s, &err);
        if (err) {
            error_propagate(errp, err);
            return;
        }

        if (s->master == ON_OFF_AUTO_ON && s->vm_id != 0) {
            error_setg(errp,
                       "master must connect to the server before any peers");
            return;
        }

        qemu_chr_fe_set_handlers(&s->server_chr, ivshmem_can_receive,
                                 ivshmem_read, NULL, NULL, s, NULL, true);

        if (ivshmem_setup_interrupts(s, errp) < 0) {
            error_prepend(errp, "Failed to initialize interrupts: ");
            return;
        }
    }

    if (s->master == ON_OFF_AUTO_AUTO) {
        s->master = s->vm_id == 0 ? ON_OFF_AUTO_ON : ON_OFF_AUTO_OFF;
    }

    if (!ivshmem_is_master(s)) {
        error_setg(&s->migration_blocker,
                   "Migration is disabled when using feature 'peer mode' in device 'ivshmem'");
        migrate_add_blocker(s->migration_blocker, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            error_free(s->migration_blocker);
            return;
        }
    }

    vmstate_register_ram(s->ivshmem_bar2, DEVICE(s));
    pci_register_bar(PCI_DEVICE(s), 2,
                     PCI_BASE_ADDRESS_SPACE_MEMORY |
                     PCI_BASE_ADDRESS_MEM_PREFETCH |
                     PCI_BASE_ADDRESS_MEM_TYPE_64,
                     s->ivshmem_bar2);
}

static void ivshmem_exit(PCIDevice *dev)
{
    IVShmemState *s = IVSHMEM_COMMON(dev);
    int i;

    if (s->migration_blocker) {
        migrate_del_blocker(s->migration_blocker);
        error_free(s->migration_blocker);
    }

    if (memory_region_is_mapped(s->ivshmem_bar2)) {
        if (!s->hostmem) {
            void *addr = memory_region_get_ram_ptr(s->ivshmem_bar2);
            int fd;

            if (munmap(addr, memory_region_size(s->ivshmem_bar2) == -1)) {
                error_report("Failed to munmap shared memory %s",
                             strerror(errno));
            }

            fd = memory_region_get_fd(s->ivshmem_bar2);
            close(fd);
        }

        vmstate_unregister_ram(s->ivshmem_bar2, DEVICE(dev));
    }

    if (s->hostmem) {
        host_memory_backend_set_mapped(s->hostmem, false);
    }

    if (s->peers) {
        for (i = 0; i < s->nb_peers; i++) {
            close_peer_eventfds(s, i);
        }
        g_free(s->peers);
    }

    if (ivshmem_has_feature(s, IVSHMEM_MSI)) {
        msix_uninit_exclusive_bar(dev);
    }

    g_free(s->msi_vectors);
}

static int ivshmem_pre_load(void *opaque)
{
    IVShmemState *s = opaque;

    if (!ivshmem_is_master(s)) {
        error_report("'peer' devices are not migratable");
        return -EINVAL;
    }

    return 0;
}

static int ivshmem_post_load(void *opaque, int version_id)
{
    IVShmemState *s = opaque;

    if (ivshmem_has_feature(s, IVSHMEM_MSI)) {
        ivshmem_msix_vector_use(s);
    }
    return 0;
}

static void ivshmem_common_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = ivshmem_common_realize;
    k->exit = ivshmem_exit;
    k->config_write = ivshmem_write_config;
    k->vendor_id = PCI_VENDOR_ID_IVSHMEM;
    k->device_id = PCI_DEVICE_ID_IVSHMEM;
    k->class_id = PCI_CLASS_MEMORY_RAM;
    k->revision = 1;
    dc->reset = ivshmem_reset;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->desc = "Inter-VM shared memory";
}

static const TypeInfo ivshmem_common_info = {
    .name          = TYPE_IVSHMEM_COMMON,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(IVShmemState),
    .abstract      = true,
    .class_init    = ivshmem_common_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static const VMStateDescription ivshmem_plain_vmsd = {
    .name = TYPE_IVSHMEM_PLAIN,
    .version_id = 0,
    .minimum_version_id = 0,
    .pre_load = ivshmem_pre_load,
    .post_load = ivshmem_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, IVShmemState),
        VMSTATE_UINT32(intrstatus, IVShmemState),
        VMSTATE_UINT32(intrmask, IVShmemState),
        VMSTATE_END_OF_LIST()
    },
};

static Property ivshmem_plain_properties[] = {
    DEFINE_PROP_ON_OFF_AUTO("master", IVShmemState, master, ON_OFF_AUTO_OFF),
    DEFINE_PROP_LINK("memdev", IVShmemState, hostmem, TYPE_MEMORY_BACKEND,
                     HostMemoryBackend *),
    DEFINE_PROP_END_OF_LIST(),
};

static void ivshmem_plain_realize(PCIDevice *dev, Error **errp)
{
    IVShmemState *s = IVSHMEM_COMMON(dev);

    if (!s->hostmem) {
        error_setg(errp, "You must specify a 'memdev'");
        return;
    } else if (host_memory_backend_is_mapped(s->hostmem)) {
        char *path = object_get_canonical_path_component(OBJECT(s->hostmem));
        error_setg(errp, "can't use already busy memdev: %s", path);
        g_free(path);
        return;
    }

    ivshmem_common_realize(dev, errp);
}

static void ivshmem_plain_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = ivshmem_plain_realize;
    dc->props = ivshmem_plain_properties;
    dc->vmsd = &ivshmem_plain_vmsd;
}

static const TypeInfo ivshmem_plain_info = {
    .name          = TYPE_IVSHMEM_PLAIN,
    .parent        = TYPE_IVSHMEM_COMMON,
    .instance_size = sizeof(IVShmemState),
    .class_init    = ivshmem_plain_class_init,
};

static const VMStateDescription ivshmem_doorbell_vmsd = {
    .name = TYPE_IVSHMEM_DOORBELL,
    .version_id = 0,
    .minimum_version_id = 0,
    .pre_load = ivshmem_pre_load,
    .post_load = ivshmem_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, IVShmemState),
        VMSTATE_MSIX(parent_obj, IVShmemState),
        VMSTATE_UINT32(intrstatus, IVShmemState),
        VMSTATE_UINT32(intrmask, IVShmemState),
        VMSTATE_END_OF_LIST()
    },
};

static Property ivshmem_doorbell_properties[] = {
    DEFINE_PROP_CHR("chardev", IVShmemState, server_chr),
    DEFINE_PROP_UINT32("vectors", IVShmemState, vectors, 1),
    DEFINE_PROP_BIT("ioeventfd", IVShmemState, features, IVSHMEM_IOEVENTFD,
                    true),
    DEFINE_PROP_ON_OFF_AUTO("master", IVShmemState, master, ON_OFF_AUTO_OFF),
    DEFINE_PROP_END_OF_LIST(),
};

static void ivshmem_doorbell_init(Object *obj)
{
    IVShmemState *s = IVSHMEM_DOORBELL(obj);

    s->features |= (1 << IVSHMEM_MSI);
}

static void ivshmem_doorbell_realize(PCIDevice *dev, Error **errp)
{
    IVShmemState *s = IVSHMEM_COMMON(dev);

    if (!qemu_chr_fe_backend_connected(&s->server_chr)) {
        error_setg(errp, "You must specify a 'chardev'");
        return;
    }

    ivshmem_common_realize(dev, errp);
}

static void ivshmem_doorbell_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = ivshmem_doorbell_realize;
    dc->props = ivshmem_doorbell_properties;
    dc->vmsd = &ivshmem_doorbell_vmsd;
}

static const TypeInfo ivshmem_doorbell_info = {
    .name          = TYPE_IVSHMEM_DOORBELL,
    .parent        = TYPE_IVSHMEM_COMMON,
    .instance_size = sizeof(IVShmemState),
    .instance_init = ivshmem_doorbell_init,
    .class_init    = ivshmem_doorbell_class_init,
};

static void ivshmem_register_types(void)
{
    type_register_static(&ivshmem_common_info);
    type_register_static(&ivshmem_plain_info);
    type_register_static(&ivshmem_doorbell_info);
}

type_init(ivshmem_register_types)
