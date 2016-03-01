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
#include "hw/hw.h"
#include "hw/i386/pc.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "sysemu/kvm.h"
#include "migration/migration.h"
#include "qemu/error-report.h"
#include "qemu/event_notifier.h"
#include "qemu/fifo8.h"
#include "sysemu/char.h"
#include "sysemu/hostmem.h"
#include "qapi/visitor.h"
#include "exec/ram_addr.h"

#include "hw/misc/ivshmem.h"

#include <sys/mman.h>

#define PCI_VENDOR_ID_IVSHMEM   PCI_VENDOR_ID_REDHAT_QUMRANET
#define PCI_DEVICE_ID_IVSHMEM   0x1110

#define IVSHMEM_MAX_PEERS G_MAXUINT16
#define IVSHMEM_IOEVENTFD   0
#define IVSHMEM_MSI     1

#define IVSHMEM_PEER    0
#define IVSHMEM_MASTER  1

#define IVSHMEM_REG_BAR_SIZE 0x100

//#define DEBUG_IVSHMEM
#ifdef DEBUG_IVSHMEM
#define IVSHMEM_DPRINTF(fmt, ...)        \
    do {printf("IVSHMEM: " fmt, ## __VA_ARGS__); } while (0)
#else
#define IVSHMEM_DPRINTF(fmt, ...)
#endif

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
} MSIVector;

typedef struct IVShmemState {
    /*< private >*/
    PCIDevice parent_obj;
    /*< public >*/

    HostMemoryBackend *hostmem;
    uint32_t intrmask;
    uint32_t intrstatus;

    CharDriverState **eventfd_chr;
    CharDriverState *server_chr;
    Fifo8 incoming_fifo;
    MemoryRegion ivshmem_mmio;

    /* We might need to register the BAR before we actually have the memory.
     * So prepare a container MemoryRegion for the BAR immediately and
     * add a subregion when we have the memory.
     */
    MemoryRegion bar;
    MemoryRegion ivshmem;
    uint64_t ivshmem_size; /* size of shared memory region */
    uint32_t ivshmem_64bit;

    Peer *peers;
    int nb_peers; /* how many peers we have space for */

    int vm_id;
    uint32_t vectors;
    uint32_t features;
    MSIVector *msi_vectors;

    Error *migration_blocker;

    char * shmobj;
    char * sizearg;
    char * role;
    int role_val;   /* scalar to avoid multiple string comparisons */
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

/* accessing registers - based on rtl8139 */
static void ivshmem_update_irq(IVShmemState *s)
{
    PCIDevice *d = PCI_DEVICE(s);
    int isr;
    isr = (s->intrstatus & s->intrmask) & 0xffffffff;

    /* don't print ISR resets */
    if (isr) {
        IVSHMEM_DPRINTF("Set IRQ to %d (%04x %04x)\n",
                        isr ? 1 : 0, s->intrstatus, s->intrmask);
    }

    pci_set_irq(d, (isr != 0));
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
            /* return my VM ID if the memory is mapped */
            if (memory_region_is_mapped(&s->ivshmem)) {
                ret = s->vm_id;
            } else {
                ret = -1;
            }
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

static int ivshmem_can_receive(void * opaque)
{
    return sizeof(int64_t);
}

static void ivshmem_event(void *opaque, int event)
{
    IVSHMEM_DPRINTF("ivshmem_event %d\n", event);
}

static void ivshmem_vector_notify(void *opaque)
{
    MSIVector *entry = opaque;
    PCIDevice *pdev = entry->pdev;
    IVShmemState *s = IVSHMEM(pdev);
    int vector = entry - s->msi_vectors;
    EventNotifier *n = &s->peers[s->vm_id].eventfds[vector];

    if (!event_notifier_test_and_clear(n)) {
        return;
    }

    IVSHMEM_DPRINTF("interrupt on vector %p %d\n", pdev, vector);
    if (ivshmem_has_feature(s, IVSHMEM_MSI)) {
        msix_notify(pdev, vector);
    } else {
        ivshmem_IntrStatus_write(s, 1);
    }
}

static int ivshmem_vector_unmask(PCIDevice *dev, unsigned vector,
                                 MSIMessage msg)
{
    IVShmemState *s = IVSHMEM(dev);
    EventNotifier *n = &s->peers[s->vm_id].eventfds[vector];
    MSIVector *v = &s->msi_vectors[vector];
    int ret;

    IVSHMEM_DPRINTF("vector unmask %p %d\n", dev, vector);

    ret = kvm_irqchip_update_msi_route(kvm_state, v->virq, msg, dev);
    if (ret < 0) {
        return ret;
    }

    return kvm_irqchip_add_irqfd_notifier_gsi(kvm_state, n, NULL, v->virq);
}

static void ivshmem_vector_mask(PCIDevice *dev, unsigned vector)
{
    IVShmemState *s = IVSHMEM(dev);
    EventNotifier *n = &s->peers[s->vm_id].eventfds[vector];
    int ret;

    IVSHMEM_DPRINTF("vector mask %p %d\n", dev, vector);

    ret = kvm_irqchip_remove_irqfd_notifier_gsi(kvm_state, n,
                                                s->msi_vectors[vector].virq);
    if (ret != 0) {
        error_report("remove_irqfd_notifier_gsi failed");
    }
}

static void ivshmem_vector_poll(PCIDevice *dev,
                                unsigned int vector_start,
                                unsigned int vector_end)
{
    IVShmemState *s = IVSHMEM(dev);
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

    /* if MSI is supported we need multiple interrupts */
    s->msi_vectors[vector].pdev = PCI_DEVICE(s);

    qemu_set_fd_handler(eventfd, ivshmem_vector_notify,
                        NULL, &s->msi_vectors[vector]);
}

static int check_shm_size(IVShmemState *s, int fd, Error **errp)
{
    /* check that the guest isn't going to try and map more memory than the
     * the object has allocated return -1 to indicate error */

    struct stat buf;

    if (fstat(fd, &buf) < 0) {
        error_setg(errp, "exiting: fstat on fd %d failed: %s",
                   fd, strerror(errno));
        return -1;
    }

    if (s->ivshmem_size > buf.st_size) {
        error_setg(errp, "Requested memory size greater"
                   " than shared object size (%" PRIu64 " > %" PRIu64")",
                   s->ivshmem_size, (uint64_t)buf.st_size);
        return -1;
    } else {
        return 0;
    }
}

/* create the shared memory BAR when we are not using the server, so we can
 * create the BAR and map the memory immediately */
static int create_shared_memory_BAR(IVShmemState *s, int fd, uint8_t attr,
                                    Error **errp)
{
    void * ptr;

    ptr = mmap(0, s->ivshmem_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        error_setg_errno(errp, errno, "Failed to mmap shared memory");
        return -1;
    }

    memory_region_init_ram_ptr(&s->ivshmem, OBJECT(s), "ivshmem.bar2",
                               s->ivshmem_size, ptr);
    qemu_set_ram_fd(memory_region_get_ram_addr(&s->ivshmem), fd);
    vmstate_register_ram(&s->ivshmem, DEVICE(s));
    memory_region_add_subregion(&s->bar, 0, &s->ivshmem);

    /* region for shared memory */
    pci_register_bar(PCI_DEVICE(s), 2, attr, &s->bar);

    return 0;
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

    if (!ivshmem_has_feature(s, IVSHMEM_IOEVENTFD)) {
        return;
    }
    if (posn < 0 || posn >= s->nb_peers) {
        error_report("invalid peer %d", posn);
        return;
    }

    n = s->peers[posn].nb_eventfds;

    memory_region_transaction_begin();
    for (i = 0; i < n; i++) {
        ivshmem_del_eventfd(s, posn, i);
    }
    memory_region_transaction_commit();
    for (i = 0; i < n; i++) {
        event_notifier_cleanup(&s->peers[posn].eventfds[i]);
    }

    g_free(s->peers[posn].eventfds);
    s->peers[posn].nb_eventfds = 0;
}

/* this function increase the dynamic storage need to store data about other
 * peers */
static int resize_peers(IVShmemState *s, int new_min_size)
{

    int j, old_size;

    /* limit number of max peers */
    if (new_min_size <= 0 || new_min_size > IVSHMEM_MAX_PEERS) {
        return -1;
    }
    if (new_min_size <= s->nb_peers) {
        return 0;
    }

    old_size = s->nb_peers;
    s->nb_peers = new_min_size;

    IVSHMEM_DPRINTF("bumping storage to %d peers\n", s->nb_peers);

    s->peers = g_realloc(s->peers, s->nb_peers * sizeof(Peer));

    for (j = old_size; j < s->nb_peers; j++) {
        s->peers[j].eventfds = g_new0(EventNotifier, s->vectors);
        s->peers[j].nb_eventfds = 0;
    }

    return 0;
}

static bool fifo_update_and_get(IVShmemState *s, const uint8_t *buf, int size,
                                void *data, size_t len)
{
    const uint8_t *p;
    uint32_t num;

    assert(len <= sizeof(int64_t)); /* limitation of the fifo */
    if (fifo8_is_empty(&s->incoming_fifo) && size == len) {
        memcpy(data, buf, size);
        return true;
    }

    IVSHMEM_DPRINTF("short read of %d bytes\n", size);

    num = MIN(size, sizeof(int64_t) - fifo8_num_used(&s->incoming_fifo));
    fifo8_push_all(&s->incoming_fifo, buf, num);

    if (fifo8_num_used(&s->incoming_fifo) < len) {
        assert(num == 0);
        return false;
    }

    size -= num;
    buf += num;
    p = fifo8_pop_buf(&s->incoming_fifo, len, &num);
    assert(num == len);

    memcpy(data, p, len);

    if (size > 0) {
        fifo8_push_all(&s->incoming_fifo, buf, size);
    }

    return true;
}

static bool fifo_update_and_get_i64(IVShmemState *s,
                                    const uint8_t *buf, int size, int64_t *i64)
{
    if (fifo_update_and_get(s, buf, size, i64, sizeof(*i64))) {
        *i64 = GINT64_FROM_LE(*i64);
        return true;
    }

    return false;
}

static int ivshmem_add_kvm_msi_virq(IVShmemState *s, int vector)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    MSIMessage msg = msix_get_message(pdev, vector);
    int ret;

    IVSHMEM_DPRINTF("ivshmem_add_kvm_msi_virq vector:%d\n", vector);

    if (s->msi_vectors[vector].pdev != NULL) {
        return 0;
    }

    ret = kvm_irqchip_add_msi_route(kvm_state, msg, pdev);
    if (ret < 0) {
        error_report("ivshmem: kvm_irqchip_add_msi_route failed");
        return -1;
    }

    s->msi_vectors[vector].virq = ret;
    s->msi_vectors[vector].pdev = pdev;

    return 0;
}

static void setup_interrupt(IVShmemState *s, int vector)
{
    EventNotifier *n = &s->peers[s->vm_id].eventfds[vector];
    bool with_irqfd = kvm_msi_via_irqfd_enabled() &&
        ivshmem_has_feature(s, IVSHMEM_MSI);
    PCIDevice *pdev = PCI_DEVICE(s);

    IVSHMEM_DPRINTF("setting up interrupt for vector: %d\n", vector);

    if (!with_irqfd) {
        IVSHMEM_DPRINTF("with eventfd");
        watch_vector_notifier(s, n, vector);
    } else if (msix_enabled(pdev)) {
        IVSHMEM_DPRINTF("with irqfd");
        if (ivshmem_add_kvm_msi_virq(s, vector) < 0) {
            return;
        }

        if (!msix_is_masked(pdev, vector)) {
            kvm_irqchip_add_irqfd_notifier_gsi(kvm_state, n, NULL,
                                               s->msi_vectors[vector].virq);
        }
    } else {
        /* it will be delayed until msix is enabled, in write_config */
        IVSHMEM_DPRINTF("with irqfd, delayed until msix enabled");
    }
}

static void ivshmem_read(void *opaque, const uint8_t *buf, int size)
{
    IVShmemState *s = opaque;
    int incoming_fd;
    int new_eventfd;
    int64_t incoming_posn;
    Error *err = NULL;
    Peer *peer;

    if (!fifo_update_and_get_i64(s, buf, size, &incoming_posn)) {
        return;
    }

    if (incoming_posn < -1) {
        IVSHMEM_DPRINTF("invalid incoming_posn %" PRId64 "\n", incoming_posn);
        return;
    }

    /* pick off s->server_chr->msgfd and store it, posn should accompany msg */
    incoming_fd = qemu_chr_fe_get_msgfd(s->server_chr);
    IVSHMEM_DPRINTF("posn is %" PRId64 ", fd is %d\n",
                    incoming_posn, incoming_fd);

    /* make sure we have enough space for this peer */
    if (incoming_posn >= s->nb_peers) {
        if (resize_peers(s, incoming_posn + 1) < 0) {
            error_report("failed to resize peers array");
            if (incoming_fd != -1) {
                close(incoming_fd);
            }
            return;
        }
    }

    peer = &s->peers[incoming_posn];

    if (incoming_fd == -1) {
        /* if posn is positive and unseen before then this is our posn*/
        if (incoming_posn >= 0 && s->vm_id == -1) {
            /* receive our posn */
            s->vm_id = incoming_posn;
        } else {
            /* otherwise an fd == -1 means an existing peer has gone away */
            IVSHMEM_DPRINTF("posn %" PRId64 " has gone away\n", incoming_posn);
            close_peer_eventfds(s, incoming_posn);
        }
        return;
    }

    /* if the position is -1, then it's shared memory region fd */
    if (incoming_posn == -1) {
        void * map_ptr;

        if (memory_region_is_mapped(&s->ivshmem)) {
            error_report("shm already initialized");
            close(incoming_fd);
            return;
        }

        if (check_shm_size(s, incoming_fd, &err) == -1) {
            error_report_err(err);
            close(incoming_fd);
            return;
        }

        /* mmap the region and map into the BAR2 */
        map_ptr = mmap(0, s->ivshmem_size, PROT_READ|PROT_WRITE, MAP_SHARED,
                                                            incoming_fd, 0);
        if (map_ptr == MAP_FAILED) {
            error_report("Failed to mmap shared memory %s", strerror(errno));
            close(incoming_fd);
            return;
        }
        memory_region_init_ram_ptr(&s->ivshmem, OBJECT(s),
                                   "ivshmem.bar2", s->ivshmem_size, map_ptr);
        qemu_set_ram_fd(memory_region_get_ram_addr(&s->ivshmem),
                        incoming_fd);
        vmstate_register_ram(&s->ivshmem, DEVICE(s));

        IVSHMEM_DPRINTF("guest h/w addr = %p, size = %" PRIu64 "\n",
                        map_ptr, s->ivshmem_size);

        memory_region_add_subregion(&s->bar, 0, &s->ivshmem);

        return;
    }

    /* each peer has an associated array of eventfds, and we keep
     * track of how many eventfds received so far */
    /* get a new eventfd: */
    if (peer->nb_eventfds >= s->vectors) {
        error_report("Too many eventfd received, device has %d vectors",
                     s->vectors);
        close(incoming_fd);
        return;
    }

    new_eventfd = peer->nb_eventfds++;

    /* this is an eventfd for a particular peer VM */
    IVSHMEM_DPRINTF("eventfds[%" PRId64 "][%d] = %d\n", incoming_posn,
                    new_eventfd, incoming_fd);
    event_notifier_init_fd(&peer->eventfds[new_eventfd], incoming_fd);
    fcntl_setfl(incoming_fd, O_NONBLOCK); /* msix/irqfd poll non block */

    if (incoming_posn == s->vm_id) {
        setup_interrupt(s, new_eventfd);
    }

    if (ivshmem_has_feature(s, IVSHMEM_IOEVENTFD)) {
        ivshmem_add_eventfd(s, incoming_posn, new_eventfd);
    }
}

static void ivshmem_check_version(void *opaque, const uint8_t * buf, int size)
{
    IVShmemState *s = opaque;
    int tmp;
    int64_t version;

    if (!fifo_update_and_get_i64(s, buf, size, &version)) {
        return;
    }

    tmp = qemu_chr_fe_get_msgfd(s->server_chr);
    if (tmp != -1 || version != IVSHMEM_PROTOCOL_VERSION) {
        fprintf(stderr, "incompatible version, you are connecting to a ivshmem-"
                "server using a different protocol please check your setup\n");
        qemu_chr_delete(s->server_chr);
        s->server_chr = NULL;
        return;
    }

    IVSHMEM_DPRINTF("version check ok, switch to real chardev handler\n");
    qemu_chr_add_handlers(s->server_chr, ivshmem_can_receive, ivshmem_read,
                          ivshmem_event, s);
}

/* Select the MSI-X vectors used by device.
 * ivshmem maps events to vectors statically, so
 * we just enable all vectors on init and after reset. */
static void ivshmem_use_msix(IVShmemState * s)
{
    PCIDevice *d = PCI_DEVICE(s);
    int i;

    IVSHMEM_DPRINTF("%s, msix present: %d\n", __func__, msix_present(d));
    if (!msix_present(d)) {
        return;
    }

    for (i = 0; i < s->vectors; i++) {
        msix_vector_use(d, i);
    }
}

static void ivshmem_reset(DeviceState *d)
{
    IVShmemState *s = IVSHMEM(d);

    s->intrstatus = 0;
    s->intrmask = 0;
    ivshmem_use_msix(s);
}

static int ivshmem_setup_interrupts(IVShmemState *s)
{
    /* allocate QEMU callback data for receiving interrupts */
    s->msi_vectors = g_malloc0(s->vectors * sizeof(MSIVector));

    if (ivshmem_has_feature(s, IVSHMEM_MSI)) {
        if (msix_init_exclusive_bar(PCI_DEVICE(s), s->vectors, 1)) {
            return -1;
        }

        IVSHMEM_DPRINTF("msix initialized (%d vectors)\n", s->vectors);
        ivshmem_use_msix(s);
    }

    return 0;
}

static void ivshmem_enable_irqfd(IVShmemState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    int i;

    for (i = 0; i < s->peers[s->vm_id].nb_eventfds; i++) {
        ivshmem_add_kvm_msi_virq(s, i);
    }

    if (msix_set_vector_notifiers(pdev,
                                  ivshmem_vector_unmask,
                                  ivshmem_vector_mask,
                                  ivshmem_vector_poll)) {
        error_report("ivshmem: msix_set_vector_notifiers failed");
    }
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

static void ivshmem_disable_irqfd(IVShmemState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    int i;

    for (i = 0; i < s->peers[s->vm_id].nb_eventfds; i++) {
        ivshmem_remove_kvm_msi_virq(s, i);
    }

    msix_unset_vector_notifiers(pdev);
}

static void ivshmem_write_config(PCIDevice *pdev, uint32_t address,
                                 uint32_t val, int len)
{
    IVShmemState *s = IVSHMEM(pdev);
    int is_enabled, was_enabled = msix_enabled(pdev);

    pci_default_write_config(pdev, address, val, len);
    is_enabled = msix_enabled(pdev);

    if (kvm_msi_via_irqfd_enabled() && s->vm_id != -1) {
        if (!was_enabled && is_enabled) {
            ivshmem_enable_irqfd(s);
        } else if (was_enabled && !is_enabled) {
            ivshmem_disable_irqfd(s);
        }
    }
}

static void pci_ivshmem_realize(PCIDevice *dev, Error **errp)
{
    IVShmemState *s = IVSHMEM(dev);
    uint8_t *pci_conf;
    uint8_t attr = PCI_BASE_ADDRESS_SPACE_MEMORY |
        PCI_BASE_ADDRESS_MEM_PREFETCH;

    if (!!s->server_chr + !!s->shmobj + !!s->hostmem != 1) {
        error_setg(errp,
                   "You must specify either 'shm', 'chardev' or 'x-memdev'");
        return;
    }

    if (s->hostmem) {
        MemoryRegion *mr;

        if (s->sizearg) {
            g_warning("size argument ignored with hostmem");
        }

        mr = host_memory_backend_get_memory(s->hostmem, errp);
        s->ivshmem_size = memory_region_size(mr);
    } else if (s->sizearg == NULL) {
        s->ivshmem_size = 4 << 20; /* 4 MB default */
    } else {
        char *end;
        int64_t size = qemu_strtosz(s->sizearg, &end);
        if (size < 0 || *end != '\0' || !is_power_of_2(size)) {
            error_setg(errp, "Invalid size %s", s->sizearg);
            return;
        }
        s->ivshmem_size = size;
    }

    fifo8_create(&s->incoming_fifo, sizeof(int64_t));

    /* IRQFD requires MSI */
    if (ivshmem_has_feature(s, IVSHMEM_IOEVENTFD) &&
        !ivshmem_has_feature(s, IVSHMEM_MSI)) {
        error_setg(errp, "ioeventfd/irqfd requires MSI");
        return;
    }

    /* check that role is reasonable */
    if (s->role) {
        if (strncmp(s->role, "peer", 5) == 0) {
            s->role_val = IVSHMEM_PEER;
        } else if (strncmp(s->role, "master", 7) == 0) {
            s->role_val = IVSHMEM_MASTER;
        } else {
            error_setg(errp, "'role' must be 'peer' or 'master'");
            return;
        }
    } else {
        s->role_val = IVSHMEM_MASTER; /* default */
    }

    if (s->role_val == IVSHMEM_PEER) {
        error_setg(&s->migration_blocker,
                   "Migration is disabled when using feature 'peer mode' in device 'ivshmem'");
        migrate_add_blocker(s->migration_blocker);
    }

    pci_conf = dev->config;
    pci_conf[PCI_COMMAND] = PCI_COMMAND_IO | PCI_COMMAND_MEMORY;

    pci_config_set_interrupt_pin(pci_conf, 1);

    memory_region_init_io(&s->ivshmem_mmio, OBJECT(s), &ivshmem_mmio_ops, s,
                          "ivshmem-mmio", IVSHMEM_REG_BAR_SIZE);

    /* region for registers*/
    pci_register_bar(dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY,
                     &s->ivshmem_mmio);

    memory_region_init(&s->bar, OBJECT(s), "ivshmem-bar2-container", s->ivshmem_size);
    if (s->ivshmem_64bit) {
        attr |= PCI_BASE_ADDRESS_MEM_TYPE_64;
    }

    if (s->hostmem != NULL) {
        MemoryRegion *mr;

        IVSHMEM_DPRINTF("using hostmem\n");

        mr = host_memory_backend_get_memory(MEMORY_BACKEND(s->hostmem), errp);
        vmstate_register_ram(mr, DEVICE(s));
        memory_region_add_subregion(&s->bar, 0, mr);
        pci_register_bar(PCI_DEVICE(s), 2, attr, &s->bar);
    } else if (s->server_chr != NULL) {
        /* FIXME do not rely on what chr drivers put into filename */
        if (strncmp(s->server_chr->filename, "unix:", 5)) {
            error_setg(errp, "chardev is not a unix client socket");
            return;
        }

        /* if we get a UNIX socket as the parameter we will talk
         * to the ivshmem server to receive the memory region */

        IVSHMEM_DPRINTF("using shared memory server (socket = %s)\n",
                        s->server_chr->filename);

        if (ivshmem_setup_interrupts(s) < 0) {
            error_setg(errp, "failed to initialize interrupts");
            return;
        }

        /* we allocate enough space for 16 peers and grow as needed */
        resize_peers(s, 16);
        s->vm_id = -1;

        pci_register_bar(dev, 2, attr, &s->bar);

        s->eventfd_chr = g_malloc0(s->vectors * sizeof(CharDriverState *));

        qemu_chr_add_handlers(s->server_chr, ivshmem_can_receive,
                              ivshmem_check_version, ivshmem_event, s);
    } else {
        /* just map the file immediately, we're not using a server */
        int fd;

        IVSHMEM_DPRINTF("using shm_open (shm object = %s)\n", s->shmobj);

        /* try opening with O_EXCL and if it succeeds zero the memory
         * by truncating to 0 */
        if ((fd = shm_open(s->shmobj, O_CREAT|O_RDWR|O_EXCL,
                        S_IRWXU|S_IRWXG|S_IRWXO)) > 0) {
           /* truncate file to length PCI device's memory */
            if (ftruncate(fd, s->ivshmem_size) != 0) {
                error_report("could not truncate shared file");
            }

        } else if ((fd = shm_open(s->shmobj, O_CREAT|O_RDWR,
                        S_IRWXU|S_IRWXG|S_IRWXO)) < 0) {
            error_setg(errp, "could not open shared file");
            return;
        }

        if (check_shm_size(s, fd, errp) == -1) {
            return;
        }

        create_shared_memory_BAR(s, fd, attr, errp);
    }
}

static void pci_ivshmem_exit(PCIDevice *dev)
{
    IVShmemState *s = IVSHMEM(dev);
    int i;

    fifo8_destroy(&s->incoming_fifo);

    if (s->migration_blocker) {
        migrate_del_blocker(s->migration_blocker);
        error_free(s->migration_blocker);
    }

    if (memory_region_is_mapped(&s->ivshmem)) {
        if (!s->hostmem) {
            void *addr = memory_region_get_ram_ptr(&s->ivshmem);
            int fd;

            if (munmap(addr, s->ivshmem_size) == -1) {
                error_report("Failed to munmap shared memory %s",
                             strerror(errno));
            }

            fd = qemu_get_ram_fd(memory_region_get_ram_addr(&s->ivshmem));
            if (fd != -1) {
                close(fd);
            }
        }

        vmstate_unregister_ram(&s->ivshmem, DEVICE(dev));
        memory_region_del_subregion(&s->bar, &s->ivshmem);
    }

    if (s->eventfd_chr) {
        for (i = 0; i < s->vectors; i++) {
            if (s->eventfd_chr[i]) {
                qemu_chr_free(s->eventfd_chr[i]);
            }
        }
        g_free(s->eventfd_chr);
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

static bool test_msix(void *opaque, int version_id)
{
    IVShmemState *s = opaque;

    return ivshmem_has_feature(s, IVSHMEM_MSI);
}

static bool test_no_msix(void *opaque, int version_id)
{
    return !test_msix(opaque, version_id);
}

static int ivshmem_pre_load(void *opaque)
{
    IVShmemState *s = opaque;

    if (s->role_val == IVSHMEM_PEER) {
        error_report("'peer' devices are not migratable");
        return -EINVAL;
    }

    return 0;
}

static int ivshmem_post_load(void *opaque, int version_id)
{
    IVShmemState *s = opaque;

    if (ivshmem_has_feature(s, IVSHMEM_MSI)) {
        ivshmem_use_msix(s);
    }

    return 0;
}

static int ivshmem_load_old(QEMUFile *f, void *opaque, int version_id)
{
    IVShmemState *s = opaque;
    PCIDevice *pdev = PCI_DEVICE(s);
    int ret;

    IVSHMEM_DPRINTF("ivshmem_load_old\n");

    if (version_id != 0) {
        return -EINVAL;
    }

    if (s->role_val == IVSHMEM_PEER) {
        error_report("'peer' devices are not migratable");
        return -EINVAL;
    }

    ret = pci_device_load(pdev, f);
    if (ret) {
        return ret;
    }

    if (ivshmem_has_feature(s, IVSHMEM_MSI)) {
        msix_load(pdev, f);
        ivshmem_use_msix(s);
    } else {
        s->intrstatus = qemu_get_be32(f);
        s->intrmask = qemu_get_be32(f);
    }

    return 0;
}

static const VMStateDescription ivshmem_vmsd = {
    .name = "ivshmem",
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_load = ivshmem_pre_load,
    .post_load = ivshmem_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, IVShmemState),

        VMSTATE_MSIX_TEST(parent_obj, IVShmemState, test_msix),
        VMSTATE_UINT32_TEST(intrstatus, IVShmemState, test_no_msix),
        VMSTATE_UINT32_TEST(intrmask, IVShmemState, test_no_msix),

        VMSTATE_END_OF_LIST()
    },
    .load_state_old = ivshmem_load_old,
    .minimum_version_id_old = 0
};

static Property ivshmem_properties[] = {
    DEFINE_PROP_CHR("chardev", IVShmemState, server_chr),
    DEFINE_PROP_STRING("size", IVShmemState, sizearg),
    DEFINE_PROP_UINT32("vectors", IVShmemState, vectors, 1),
    DEFINE_PROP_BIT("ioeventfd", IVShmemState, features, IVSHMEM_IOEVENTFD, false),
    DEFINE_PROP_BIT("msi", IVShmemState, features, IVSHMEM_MSI, true),
    DEFINE_PROP_STRING("shm", IVShmemState, shmobj),
    DEFINE_PROP_STRING("role", IVShmemState, role),
    DEFINE_PROP_UINT32("use64", IVShmemState, ivshmem_64bit, 1),
    DEFINE_PROP_END_OF_LIST(),
};

static void ivshmem_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pci_ivshmem_realize;
    k->exit = pci_ivshmem_exit;
    k->config_write = ivshmem_write_config;
    k->vendor_id = PCI_VENDOR_ID_IVSHMEM;
    k->device_id = PCI_DEVICE_ID_IVSHMEM;
    k->class_id = PCI_CLASS_MEMORY_RAM;
    dc->reset = ivshmem_reset;
    dc->props = ivshmem_properties;
    dc->vmsd = &ivshmem_vmsd;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->desc = "Inter-VM shared memory";
}

static void ivshmem_check_memdev_is_busy(Object *obj, const char *name,
                                         Object *val, Error **errp)
{
    MemoryRegion *mr;

    mr = host_memory_backend_get_memory(MEMORY_BACKEND(val), errp);
    if (memory_region_is_mapped(mr)) {
        char *path = object_get_canonical_path_component(val);
        error_setg(errp, "can't use already busy memdev: %s", path);
        g_free(path);
    } else {
        qdev_prop_allow_set_link_before_realize(obj, name, val, errp);
    }
}

static void ivshmem_init(Object *obj)
{
    IVShmemState *s = IVSHMEM(obj);

    object_property_add_link(obj, "x-memdev", TYPE_MEMORY_BACKEND,
                             (Object **)&s->hostmem,
                             ivshmem_check_memdev_is_busy,
                             OBJ_PROP_LINK_UNREF_ON_RELEASE,
                             &error_abort);
}

static const TypeInfo ivshmem_info = {
    .name          = TYPE_IVSHMEM,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(IVShmemState),
    .instance_init = ivshmem_init,
    .class_init    = ivshmem_class_init,
};

static void ivshmem_register_types(void)
{
    type_register_static(&ivshmem_info);
}

type_init(ivshmem_register_types)
