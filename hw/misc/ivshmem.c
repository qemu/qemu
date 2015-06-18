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
#include "hw/hw.h"
#include "hw/i386/pc.h"
#include "hw/pci/pci.h"
#include "hw/pci/msix.h"
#include "sysemu/kvm.h"
#include "migration/migration.h"
#include "qemu/error-report.h"
#include "qemu/event_notifier.h"
#include "qemu/fifo8.h"
#include "sysemu/char.h"

#include <sys/mman.h>
#include <sys/types.h>
#include <limits.h>

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

typedef struct EventfdEntry {
    PCIDevice *pdev;
    int vector;
} EventfdEntry;

typedef struct IVShmemState {
    /*< private >*/
    PCIDevice parent_obj;
    /*< public >*/

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
    int shm_fd; /* shared memory file descriptor */

    Peer *peers;
    int nb_peers; /* how many guests we have space for */

    int vm_id;
    uint32_t vectors;
    uint32_t features;
    EventfdEntry *eventfd_table;

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
            if (s->shm_fd > 0) {
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

static void ivshmem_receive(void *opaque, const uint8_t *buf, int size)
{
    IVShmemState *s = opaque;

    IVSHMEM_DPRINTF("ivshmem_receive 0x%02x size: %d\n", *buf, size);

    ivshmem_IntrStatus_write(s, *buf);
}

static int ivshmem_can_receive(void * opaque)
{
    return sizeof(long);
}

static void ivshmem_event(void *opaque, int event)
{
    IVSHMEM_DPRINTF("ivshmem_event %d\n", event);
}

static void fake_irqfd(void *opaque, const uint8_t *buf, int size) {

    EventfdEntry *entry = opaque;
    PCIDevice *pdev = entry->pdev;

    IVSHMEM_DPRINTF("interrupt on vector %p %d\n", pdev, entry->vector);
    msix_notify(pdev, entry->vector);
}

static CharDriverState* create_eventfd_chr_device(void * opaque, EventNotifier *n,
                                                  int vector)
{
    /* create a event character device based on the passed eventfd */
    IVShmemState *s = opaque;
    CharDriverState * chr;
    int eventfd = event_notifier_get_fd(n);

    chr = qemu_chr_open_eventfd(eventfd);

    if (chr == NULL) {
        error_report("creating chardriver for eventfd %d failed", eventfd);
        return NULL;
    }
    qemu_chr_fe_claim_no_fail(chr);

    /* if MSI is supported we need multiple interrupts */
    if (ivshmem_has_feature(s, IVSHMEM_MSI)) {
        s->eventfd_table[vector].pdev = PCI_DEVICE(s);
        s->eventfd_table[vector].vector = vector;

        qemu_chr_add_handlers(chr, ivshmem_can_receive, fake_irqfd,
                      ivshmem_event, &s->eventfd_table[vector]);
    } else {
        qemu_chr_add_handlers(chr, ivshmem_can_receive, ivshmem_receive,
                      ivshmem_event, s);
    }

    return chr;

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

    s->shm_fd = fd;

    memory_region_init_ram_ptr(&s->ivshmem, OBJECT(s), "ivshmem.bar2",
                               s->ivshmem_size, ptr);
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

static void close_guest_eventfds(IVShmemState *s, int posn)
{
    int i, guest_curr_max;

    if (!ivshmem_has_feature(s, IVSHMEM_IOEVENTFD)) {
        return;
    }
    if (posn < 0 || posn >= s->nb_peers) {
        error_report("invalid peer %d", posn);
        return;
    }

    guest_curr_max = s->peers[posn].nb_eventfds;

    memory_region_transaction_begin();
    for (i = 0; i < guest_curr_max; i++) {
        ivshmem_del_eventfd(s, posn, i);
    }
    memory_region_transaction_commit();
    for (i = 0; i < guest_curr_max; i++) {
        event_notifier_cleanup(&s->peers[posn].eventfds[i]);
    }

    g_free(s->peers[posn].eventfds);
    s->peers[posn].nb_eventfds = 0;
}

/* this function increase the dynamic storage need to store data about other
 * guests */
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

    IVSHMEM_DPRINTF("bumping storage to %d guests\n", s->nb_peers);

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

    assert(len <= sizeof(long)); /* limitation of the fifo */
    if (fifo8_is_empty(&s->incoming_fifo) && size == len) {
        memcpy(data, buf, size);
        return true;
    }

    IVSHMEM_DPRINTF("short read of %d bytes\n", size);

    num = MIN(size, sizeof(long) - fifo8_num_used(&s->incoming_fifo));
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

static void ivshmem_read(void *opaque, const uint8_t *buf, int size)
{
    IVShmemState *s = opaque;
    int incoming_fd;
    int new_eventfd;
    long incoming_posn;
    Error *err = NULL;
    Peer *peer;

    if (!fifo_update_and_get(s, buf, size,
                             &incoming_posn, sizeof(incoming_posn))) {
        return;
    }

    if (incoming_posn < -1) {
        IVSHMEM_DPRINTF("invalid incoming_posn %ld\n", incoming_posn);
        return;
    }

    /* pick off s->server_chr->msgfd and store it, posn should accompany msg */
    incoming_fd = qemu_chr_fe_get_msgfd(s->server_chr);
    IVSHMEM_DPRINTF("posn is %ld, fd is %d\n", incoming_posn, incoming_fd);

    /* make sure we have enough space for this guest */
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
            /* otherwise an fd == -1 means an existing guest has gone away */
            IVSHMEM_DPRINTF("posn %ld has gone away\n", incoming_posn);
            close_guest_eventfds(s, incoming_posn);
        }
        return;
    }

    /* if the position is -1, then it's shared memory region fd */
    if (incoming_posn == -1) {
        void * map_ptr;

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
        vmstate_register_ram(&s->ivshmem, DEVICE(s));

        IVSHMEM_DPRINTF("guest h/w addr = %p, size = %" PRIu64 "\n",
                        map_ptr, s->ivshmem_size);

        memory_region_add_subregion(&s->bar, 0, &s->ivshmem);

        /* only store the fd if it is successfully mapped */
        s->shm_fd = incoming_fd;

        return;
    }

    /* each peer has an associated array of eventfds, and we keep
     * track of how many eventfds received so far */
    /* get a new eventfd: */
    new_eventfd = peer->nb_eventfds++;

    /* this is an eventfd for a particular guest VM */
    IVSHMEM_DPRINTF("eventfds[%ld][%d] = %d\n", incoming_posn,
                    new_eventfd, incoming_fd);
    event_notifier_init_fd(&peer->eventfds[new_eventfd], incoming_fd);

    if (incoming_posn == s->vm_id) {
        s->eventfd_chr[new_eventfd] = create_eventfd_chr_device(s,
                   &s->peers[s->vm_id].eventfds[new_eventfd],
                   new_eventfd);
    }

    if (ivshmem_has_feature(s, IVSHMEM_IOEVENTFD)) {
        ivshmem_add_eventfd(s, incoming_posn, new_eventfd);
    }
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
    ivshmem_use_msix(s);
}

static uint64_t ivshmem_get_size(IVShmemState * s, Error **errp) {

    uint64_t value;
    char *ptr;

    value = strtoull(s->sizearg, &ptr, 10);
    switch (*ptr) {
        case 0: case 'M': case 'm':
            value <<= 20;
            break;
        case 'G': case 'g':
            value <<= 30;
            break;
        default:
            error_setg(errp, "invalid ram size: %s", s->sizearg);
            return 0;
    }

    /* BARs must be a power of 2 */
    if (!is_power_of_2(value)) {
        error_setg(errp, "size must be power of 2");
        return 0;
    }

    return value;
}

static int ivshmem_setup_msi(IVShmemState * s)
{
    if (msix_init_exclusive_bar(PCI_DEVICE(s), s->vectors, 1)) {
        return -1;
    }

    IVSHMEM_DPRINTF("msix initialized (%d vectors)\n", s->vectors);

    /* allocate QEMU char devices for receiving interrupts */
    s->eventfd_table = g_malloc0(s->vectors * sizeof(EventfdEntry));

    ivshmem_use_msix(s);
    return 0;
}

static void ivshmem_save(QEMUFile* f, void *opaque)
{
    IVShmemState *proxy = opaque;
    PCIDevice *pci_dev = PCI_DEVICE(proxy);

    IVSHMEM_DPRINTF("ivshmem_save\n");
    pci_device_save(pci_dev, f);

    if (ivshmem_has_feature(proxy, IVSHMEM_MSI)) {
        msix_save(pci_dev, f);
    } else {
        qemu_put_be32(f, proxy->intrstatus);
        qemu_put_be32(f, proxy->intrmask);
    }

}

static int ivshmem_load(QEMUFile* f, void *opaque, int version_id)
{
    IVSHMEM_DPRINTF("ivshmem_load\n");

    IVShmemState *proxy = opaque;
    PCIDevice *pci_dev = PCI_DEVICE(proxy);
    int ret;

    if (version_id > 0) {
        return -EINVAL;
    }

    if (proxy->role_val == IVSHMEM_PEER) {
        error_report("'peer' devices are not migratable");
        return -EINVAL;
    }

    ret = pci_device_load(pci_dev, f);
    if (ret) {
        return ret;
    }

    if (ivshmem_has_feature(proxy, IVSHMEM_MSI)) {
        msix_load(pci_dev, f);
	ivshmem_use_msix(proxy);
    } else {
        proxy->intrstatus = qemu_get_be32(f);
        proxy->intrmask = qemu_get_be32(f);
    }

    return 0;
}

static void ivshmem_write_config(PCIDevice *pci_dev, uint32_t address,
                                 uint32_t val, int len)
{
    pci_default_write_config(pci_dev, address, val, len);
}

static void pci_ivshmem_realize(PCIDevice *dev, Error **errp)
{
    IVShmemState *s = IVSHMEM(dev);
    uint8_t *pci_conf;
    uint8_t attr = PCI_BASE_ADDRESS_SPACE_MEMORY |
        PCI_BASE_ADDRESS_MEM_PREFETCH;
    Error *local_err = NULL;

    if (s->sizearg == NULL) {
        s->ivshmem_size = 4 << 20; /* 4 MB default */
    } else {
        s->ivshmem_size = ivshmem_get_size(s, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
    }

    fifo8_create(&s->incoming_fifo, sizeof(long));
    register_savevm(DEVICE(dev), "ivshmem", 0, 0, ivshmem_save, ivshmem_load,
                                                                        dev);
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

    s->shm_fd = 0;

    memory_region_init_io(&s->ivshmem_mmio, OBJECT(s), &ivshmem_mmio_ops, s,
                          "ivshmem-mmio", IVSHMEM_REG_BAR_SIZE);

    /* region for registers*/
    pci_register_bar(dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY,
                     &s->ivshmem_mmio);

    memory_region_init(&s->bar, OBJECT(s), "ivshmem-bar2-container", s->ivshmem_size);
    if (s->ivshmem_64bit) {
        attr |= PCI_BASE_ADDRESS_MEM_TYPE_64;
    }

    if (s->server_chr != NULL) {
        if (strncmp(s->server_chr->filename, "unix:", 5)) {
            error_setg(errp, "chardev is not a unix client socket");
            return;
        }

        /* if we get a UNIX socket as the parameter we will talk
         * to the ivshmem server to receive the memory region */

        if (s->shmobj != NULL) {
            error_setg(errp, "do not specify both 'chardev' "
                       "and 'shm' with ivshmem");
            return;
        }

        IVSHMEM_DPRINTF("using shared memory server (socket = %s)\n",
                        s->server_chr->filename);

        if (ivshmem_has_feature(s, IVSHMEM_MSI) &&
            ivshmem_setup_msi(s)) {
            error_setg(errp, "msix initialization failed");
            return;
        }

        /* we allocate enough space for 16 guests and grow as needed */
        resize_peers(s, 16);
        s->vm_id = -1;

        pci_register_bar(dev, 2, attr, &s->bar);

        s->eventfd_chr = g_malloc0(s->vectors * sizeof(CharDriverState *));

        qemu_chr_add_handlers(s->server_chr, ivshmem_can_receive, ivshmem_read,
                     ivshmem_event, s);
    } else {
        /* just map the file immediately, we're not using a server */
        int fd;

        if (s->shmobj == NULL) {
            error_setg(errp, "Must specify 'chardev' or 'shm' to ivshmem");
            return;
        }

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

    if (s->migration_blocker) {
        migrate_del_blocker(s->migration_blocker);
        error_free(s->migration_blocker);
    }

    memory_region_del_subregion(&s->bar, &s->ivshmem);
    vmstate_unregister_ram(&s->ivshmem, DEVICE(dev));
    unregister_savevm(DEVICE(dev), "ivshmem", s);
    fifo8_destroy(&s->incoming_fifo);
}

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
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo ivshmem_info = {
    .name          = TYPE_IVSHMEM,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(IVShmemState),
    .class_init    = ivshmem_class_init,
};

static void ivshmem_register_types(void)
{
    type_register_static(&ivshmem_info);
}

type_init(ivshmem_register_types)
