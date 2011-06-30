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
 */
#include "hw.h"
#include "pc.h"
#include "pci.h"
#include "msix.h"
#include "kvm.h"

#include <sys/mman.h>
#include <sys/types.h>

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

typedef struct Peer {
    int nb_eventfds;
    int *eventfds;
} Peer;

typedef struct EventfdEntry {
    PCIDevice *pdev;
    int vector;
} EventfdEntry;

typedef struct IVShmemState {
    PCIDevice dev;
    uint32_t intrmask;
    uint32_t intrstatus;
    uint32_t doorbell;

    CharDriverState **eventfd_chr;
    CharDriverState *server_chr;
    int ivshmem_mmio_io_addr;

    pcibus_t mmio_addr;
    pcibus_t shm_pci_addr;
    uint64_t ivshmem_offset;
    uint64_t ivshmem_size; /* size of shared memory region */
    int shm_fd; /* shared memory file descriptor */

    Peer *peers;
    int nb_peers; /* how many guests we have space for */
    int max_peer; /* maximum numbered peer */

    int vm_id;
    uint32_t vectors;
    uint32_t features;
    EventfdEntry *eventfd_table;

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

static inline bool is_power_of_two(uint64_t x) {
    return (x & (x - 1)) == 0;
}

static void ivshmem_map(PCIDevice *pci_dev, int region_num,
                    pcibus_t addr, pcibus_t size, int type)
{
    IVShmemState *s = DO_UPCAST(IVShmemState, dev, pci_dev);

    s->shm_pci_addr = addr;

    if (s->ivshmem_offset > 0) {
        cpu_register_physical_memory(s->shm_pci_addr, s->ivshmem_size,
                                                            s->ivshmem_offset);
    }

    IVSHMEM_DPRINTF("guest pci addr = %" FMT_PCIBUS ", guest h/w addr = %"
        PRIu64 ", size = %" FMT_PCIBUS "\n", addr, s->ivshmem_offset, size);

}

/* accessing registers - based on rtl8139 */
static void ivshmem_update_irq(IVShmemState *s, int val)
{
    int isr;
    isr = (s->intrstatus & s->intrmask) & 0xffffffff;

    /* don't print ISR resets */
    if (isr) {
        IVSHMEM_DPRINTF("Set IRQ to %d (%04x %04x)\n",
           isr ? 1 : 0, s->intrstatus, s->intrmask);
    }

    qemu_set_irq(s->dev.irq[0], (isr != 0));
}

static void ivshmem_IntrMask_write(IVShmemState *s, uint32_t val)
{
    IVSHMEM_DPRINTF("IntrMask write(w) val = 0x%04x\n", val);

    s->intrmask = val;

    ivshmem_update_irq(s, val);
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

    ivshmem_update_irq(s, val);
    return;
}

static uint32_t ivshmem_IntrStatus_read(IVShmemState *s)
{
    uint32_t ret = s->intrstatus;

    /* reading ISR clears all interrupts */
    s->intrstatus = 0;

    ivshmem_update_irq(s, 0);

    return ret;
}

static void ivshmem_io_writew(void *opaque, target_phys_addr_t addr,
                                                            uint32_t val)
{

    IVSHMEM_DPRINTF("We shouldn't be writing words\n");
}

static void ivshmem_io_writel(void *opaque, target_phys_addr_t addr,
                                                            uint32_t val)
{
    IVShmemState *s = opaque;

    uint64_t write_one = 1;
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
            if (dest > s->max_peer) {
                IVSHMEM_DPRINTF("Invalid destination VM ID (%d)\n", dest);
                break;
            }

            /* check doorbell range */
            if (vector < s->peers[dest].nb_eventfds) {
                IVSHMEM_DPRINTF("Writing %" PRId64 " to VM %d on vector %d\n",
                                                    write_one, dest, vector);
                if (write(s->peers[dest].eventfds[vector],
                                                    &(write_one), 8) != 8) {
                    IVSHMEM_DPRINTF("error writing to eventfd\n");
                }
            }
            break;
        default:
            IVSHMEM_DPRINTF("Invalid VM Doorbell VM %d\n", dest);
    }
}

static void ivshmem_io_writeb(void *opaque, target_phys_addr_t addr,
                                                                uint32_t val)
{
    IVSHMEM_DPRINTF("We shouldn't be writing bytes\n");
}

static uint32_t ivshmem_io_readw(void *opaque, target_phys_addr_t addr)
{

    IVSHMEM_DPRINTF("We shouldn't be reading words\n");
    return 0;
}

static uint32_t ivshmem_io_readl(void *opaque, target_phys_addr_t addr)
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

static uint32_t ivshmem_io_readb(void *opaque, target_phys_addr_t addr)
{
    IVSHMEM_DPRINTF("We shouldn't be reading bytes\n");

    return 0;
}

static CPUReadMemoryFunc * const ivshmem_mmio_read[3] = {
    ivshmem_io_readb,
    ivshmem_io_readw,
    ivshmem_io_readl,
};

static CPUWriteMemoryFunc * const ivshmem_mmio_write[3] = {
    ivshmem_io_writeb,
    ivshmem_io_writew,
    ivshmem_io_writel,
};

static void ivshmem_receive(void *opaque, const uint8_t *buf, int size)
{
    IVShmemState *s = opaque;

    ivshmem_IntrStatus_write(s, *buf);

    IVSHMEM_DPRINTF("ivshmem_receive 0x%02x\n", *buf);
}

static int ivshmem_can_receive(void * opaque)
{
    return 8;
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

static CharDriverState* create_eventfd_chr_device(void * opaque, int eventfd,
                                                                    int vector)
{
    /* create a event character device based on the passed eventfd */
    IVShmemState *s = opaque;
    CharDriverState * chr;

    chr = qemu_chr_open_eventfd(eventfd);

    if (chr == NULL) {
        fprintf(stderr, "creating eventfd for eventfd %d failed\n", eventfd);
        exit(-1);
    }

    /* if MSI is supported we need multiple interrupts */
    if (ivshmem_has_feature(s, IVSHMEM_MSI)) {
        s->eventfd_table[vector].pdev = &s->dev;
        s->eventfd_table[vector].vector = vector;

        qemu_chr_add_handlers(chr, ivshmem_can_receive, fake_irqfd,
                      ivshmem_event, &s->eventfd_table[vector]);
    } else {
        qemu_chr_add_handlers(chr, ivshmem_can_receive, ivshmem_receive,
                      ivshmem_event, s);
    }

    return chr;

}

static int check_shm_size(IVShmemState *s, int fd) {
    /* check that the guest isn't going to try and map more memory than the
     * the object has allocated return -1 to indicate error */

    struct stat buf;

    fstat(fd, &buf);

    if (s->ivshmem_size > buf.st_size) {
        fprintf(stderr,
                "IVSHMEM ERROR: Requested memory size greater"
                " than shared object size (%" PRIu64 " > %" PRIu64")\n",
                s->ivshmem_size, (uint64_t)buf.st_size);
        return -1;
    } else {
        return 0;
    }
}

/* create the shared memory BAR when we are not using the server, so we can
 * create the BAR and map the memory immediately */
static void create_shared_memory_BAR(IVShmemState *s, int fd) {

    void * ptr;

    s->shm_fd = fd;

    ptr = mmap(0, s->ivshmem_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);

    s->ivshmem_offset = qemu_ram_alloc_from_ptr(&s->dev.qdev, "ivshmem.bar2",
                                                        s->ivshmem_size, ptr);

    /* region for shared memory */
    pci_register_bar(&s->dev, 2, s->ivshmem_size,
                                PCI_BASE_ADDRESS_SPACE_MEMORY, ivshmem_map);
}

static void close_guest_eventfds(IVShmemState *s, int posn)
{
    int i, guest_curr_max;

    guest_curr_max = s->peers[posn].nb_eventfds;

    for (i = 0; i < guest_curr_max; i++) {
        kvm_set_ioeventfd_mmio_long(s->peers[posn].eventfds[i],
                    s->mmio_addr + DOORBELL, (posn << 16) | i, 0);
        close(s->peers[posn].eventfds[i]);
    }

    qemu_free(s->peers[posn].eventfds);
    s->peers[posn].nb_eventfds = 0;
}

static void setup_ioeventfds(IVShmemState *s) {

    int i, j;

    for (i = 0; i <= s->max_peer; i++) {
        for (j = 0; j < s->peers[i].nb_eventfds; j++) {
            kvm_set_ioeventfd_mmio_long(s->peers[i].eventfds[j],
                    s->mmio_addr + DOORBELL, (i << 16) | j, 1);
        }
    }
}

/* this function increase the dynamic storage need to store data about other
 * guests */
static void increase_dynamic_storage(IVShmemState *s, int new_min_size) {

    int j, old_nb_alloc;

    old_nb_alloc = s->nb_peers;

    while (new_min_size >= s->nb_peers)
        s->nb_peers = s->nb_peers * 2;

    IVSHMEM_DPRINTF("bumping storage to %d guests\n", s->nb_peers);
    s->peers = qemu_realloc(s->peers, s->nb_peers * sizeof(Peer));

    /* zero out new pointers */
    for (j = old_nb_alloc; j < s->nb_peers; j++) {
        s->peers[j].eventfds = NULL;
        s->peers[j].nb_eventfds = 0;
    }
}

static void ivshmem_read(void *opaque, const uint8_t * buf, int flags)
{
    IVShmemState *s = opaque;
    int incoming_fd, tmp_fd;
    int guest_max_eventfd;
    long incoming_posn;

    memcpy(&incoming_posn, buf, sizeof(long));
    /* pick off s->server_chr->msgfd and store it, posn should accompany msg */
    tmp_fd = qemu_chr_get_msgfd(s->server_chr);
    IVSHMEM_DPRINTF("posn is %ld, fd is %d\n", incoming_posn, tmp_fd);

    /* make sure we have enough space for this guest */
    if (incoming_posn >= s->nb_peers) {
        increase_dynamic_storage(s, incoming_posn);
    }

    if (tmp_fd == -1) {
        /* if posn is positive and unseen before then this is our posn*/
        if ((incoming_posn >= 0) &&
                            (s->peers[incoming_posn].eventfds == NULL)) {
            /* receive our posn */
            s->vm_id = incoming_posn;
            return;
        } else {
            /* otherwise an fd == -1 means an existing guest has gone away */
            IVSHMEM_DPRINTF("posn %ld has gone away\n", incoming_posn);
            close_guest_eventfds(s, incoming_posn);
            return;
        }
    }

    /* because of the implementation of get_msgfd, we need a dup */
    incoming_fd = dup(tmp_fd);

    if (incoming_fd == -1) {
        fprintf(stderr, "could not allocate file descriptor %s\n",
                                                            strerror(errno));
        return;
    }

    /* if the position is -1, then it's shared memory region fd */
    if (incoming_posn == -1) {

        void * map_ptr;

        s->max_peer = 0;

        if (check_shm_size(s, incoming_fd) == -1) {
            exit(-1);
        }

        /* mmap the region and map into the BAR2 */
        map_ptr = mmap(0, s->ivshmem_size, PROT_READ|PROT_WRITE, MAP_SHARED,
                                                            incoming_fd, 0);
        s->ivshmem_offset = qemu_ram_alloc_from_ptr(&s->dev.qdev,
                                    "ivshmem.bar2", s->ivshmem_size, map_ptr);

        IVSHMEM_DPRINTF("guest pci addr = %" FMT_PCIBUS ", guest h/w addr = %"
                         PRIu64 ", size = %" PRIu64 "\n", s->shm_pci_addr,
                         s->ivshmem_offset, s->ivshmem_size);

        if (s->shm_pci_addr > 0) {
            /* map memory into BAR2 */
            cpu_register_physical_memory(s->shm_pci_addr, s->ivshmem_size,
                                                            s->ivshmem_offset);
        }

        /* only store the fd if it is successfully mapped */
        s->shm_fd = incoming_fd;

        return;
    }

    /* each guest has an array of eventfds, and we keep track of how many
     * guests for each VM */
    guest_max_eventfd = s->peers[incoming_posn].nb_eventfds;

    if (guest_max_eventfd == 0) {
        /* one eventfd per MSI vector */
        s->peers[incoming_posn].eventfds = (int *) qemu_malloc(s->vectors *
                                                                sizeof(int));
    }

    /* this is an eventfd for a particular guest VM */
    IVSHMEM_DPRINTF("eventfds[%ld][%d] = %d\n", incoming_posn,
                                            guest_max_eventfd, incoming_fd);
    s->peers[incoming_posn].eventfds[guest_max_eventfd] = incoming_fd;

    /* increment count for particular guest */
    s->peers[incoming_posn].nb_eventfds++;

    /* keep track of the maximum VM ID */
    if (incoming_posn > s->max_peer) {
        s->max_peer = incoming_posn;
    }

    if (incoming_posn == s->vm_id) {
        s->eventfd_chr[guest_max_eventfd] = create_eventfd_chr_device(s,
                   s->peers[s->vm_id].eventfds[guest_max_eventfd],
                   guest_max_eventfd);
    }

    if (ivshmem_has_feature(s, IVSHMEM_IOEVENTFD)) {
        if (kvm_set_ioeventfd_mmio_long(incoming_fd, s->mmio_addr + DOORBELL,
                        (incoming_posn << 16) | guest_max_eventfd, 1) < 0) {
            fprintf(stderr, "ivshmem: ioeventfd not available\n");
        }
    }

    return;
}

static void ivshmem_reset(DeviceState *d)
{
    IVShmemState *s = DO_UPCAST(IVShmemState, dev.qdev, d);

    s->intrstatus = 0;
    return;
}

static void ivshmem_mmio_map(PCIDevice *pci_dev, int region_num,
                       pcibus_t addr, pcibus_t size, int type)
{
    IVShmemState *s = DO_UPCAST(IVShmemState, dev, pci_dev);

    s->mmio_addr = addr;
    cpu_register_physical_memory(addr + 0, IVSHMEM_REG_BAR_SIZE,
                                                s->ivshmem_mmio_io_addr);

    if (ivshmem_has_feature(s, IVSHMEM_IOEVENTFD)) {
        setup_ioeventfds(s);
    }
}

static uint64_t ivshmem_get_size(IVShmemState * s) {

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
            fprintf(stderr, "qemu: invalid ram size: %s\n", s->sizearg);
            exit(1);
    }

    /* BARs must be a power of 2 */
    if (!is_power_of_two(value)) {
        fprintf(stderr, "ivshmem: size must be power of 2\n");
        exit(1);
    }

    return value;
}

static void ivshmem_setup_msi(IVShmemState * s) {

    int i;

    /* allocate the MSI-X vectors */

    if (!msix_init(&s->dev, s->vectors, 1, 0)) {
        pci_register_bar(&s->dev, 1,
                         msix_bar_size(&s->dev),
                         PCI_BASE_ADDRESS_SPACE_MEMORY,
                         msix_mmio_map);
        IVSHMEM_DPRINTF("msix initialized (%d vectors)\n", s->vectors);
    } else {
        IVSHMEM_DPRINTF("msix initialization failed\n");
        exit(1);
    }

    /* 'activate' the vectors */
    for (i = 0; i < s->vectors; i++) {
        msix_vector_use(&s->dev, i);
    }

    /* allocate Qemu char devices for receiving interrupts */
    s->eventfd_table = qemu_mallocz(s->vectors * sizeof(EventfdEntry));
}

static void ivshmem_save(QEMUFile* f, void *opaque)
{
    IVShmemState *proxy = opaque;

    IVSHMEM_DPRINTF("ivshmem_save\n");
    pci_device_save(&proxy->dev, f);

    if (ivshmem_has_feature(proxy, IVSHMEM_MSI)) {
        msix_save(&proxy->dev, f);
    } else {
        qemu_put_be32(f, proxy->intrstatus);
        qemu_put_be32(f, proxy->intrmask);
    }

}

static int ivshmem_load(QEMUFile* f, void *opaque, int version_id)
{
    IVSHMEM_DPRINTF("ivshmem_load\n");

    IVShmemState *proxy = opaque;
    int ret, i;

    if (version_id > 0) {
        return -EINVAL;
    }

    if (proxy->role_val == IVSHMEM_PEER) {
        fprintf(stderr, "ivshmem: 'peer' devices are not migratable\n");
        return -EINVAL;
    }

    ret = pci_device_load(&proxy->dev, f);
    if (ret) {
        return ret;
    }

    if (ivshmem_has_feature(proxy, IVSHMEM_MSI)) {
        msix_load(&proxy->dev, f);
        for (i = 0; i < proxy->vectors; i++) {
            msix_vector_use(&proxy->dev, i);
        }
    } else {
        proxy->intrstatus = qemu_get_be32(f);
        proxy->intrmask = qemu_get_be32(f);
    }

    return 0;
}

static int pci_ivshmem_init(PCIDevice *dev)
{
    IVShmemState *s = DO_UPCAST(IVShmemState, dev, dev);
    uint8_t *pci_conf;

    if (s->sizearg == NULL)
        s->ivshmem_size = 4 << 20; /* 4 MB default */
    else {
        s->ivshmem_size = ivshmem_get_size(s);
    }

    register_savevm(&s->dev.qdev, "ivshmem", 0, 0, ivshmem_save, ivshmem_load,
                                                                        dev);

    /* IRQFD requires MSI */
    if (ivshmem_has_feature(s, IVSHMEM_IOEVENTFD) &&
        !ivshmem_has_feature(s, IVSHMEM_MSI)) {
        fprintf(stderr, "ivshmem: ioeventfd/irqfd requires MSI\n");
        exit(1);
    }

    /* check that role is reasonable */
    if (s->role) {
        if (strncmp(s->role, "peer", 5) == 0) {
            s->role_val = IVSHMEM_PEER;
        } else if (strncmp(s->role, "master", 7) == 0) {
            s->role_val = IVSHMEM_MASTER;
        } else {
            fprintf(stderr, "ivshmem: 'role' must be 'peer' or 'master'\n");
            exit(1);
        }
    } else {
        s->role_val = IVSHMEM_MASTER; /* default */
    }

    if (s->role_val == IVSHMEM_PEER) {
        register_device_unmigratable(&s->dev.qdev, "ivshmem", s);
    }

    pci_conf = s->dev.config;
    pci_conf[PCI_COMMAND] = PCI_COMMAND_IO | PCI_COMMAND_MEMORY;

    pci_config_set_interrupt_pin(pci_conf, 1);

    s->shm_pci_addr = 0;
    s->ivshmem_offset = 0;
    s->shm_fd = 0;

    s->ivshmem_mmio_io_addr = cpu_register_io_memory(ivshmem_mmio_read,
                                    ivshmem_mmio_write, s, DEVICE_NATIVE_ENDIAN);
    /* region for registers*/
    pci_register_bar(&s->dev, 0, IVSHMEM_REG_BAR_SIZE,
                           PCI_BASE_ADDRESS_SPACE_MEMORY, ivshmem_mmio_map);

    if ((s->server_chr != NULL) &&
                        (strncmp(s->server_chr->filename, "unix:", 5) == 0)) {
        /* if we get a UNIX socket as the parameter we will talk
         * to the ivshmem server to receive the memory region */

        if (s->shmobj != NULL) {
            fprintf(stderr, "WARNING: do not specify both 'chardev' "
                                                "and 'shm' with ivshmem\n");
        }

        IVSHMEM_DPRINTF("using shared memory server (socket = %s)\n",
                                                    s->server_chr->filename);

        if (ivshmem_has_feature(s, IVSHMEM_MSI)) {
            ivshmem_setup_msi(s);
        }

        /* we allocate enough space for 16 guests and grow as needed */
        s->nb_peers = 16;
        s->vm_id = -1;

        /* allocate/initialize space for interrupt handling */
        s->peers = qemu_mallocz(s->nb_peers * sizeof(Peer));

        pci_register_bar(&s->dev, 2, s->ivshmem_size,
                                PCI_BASE_ADDRESS_SPACE_MEMORY, ivshmem_map);

        s->eventfd_chr = qemu_mallocz(s->vectors * sizeof(CharDriverState *));

        qemu_chr_add_handlers(s->server_chr, ivshmem_can_receive, ivshmem_read,
                     ivshmem_event, s);
    } else {
        /* just map the file immediately, we're not using a server */
        int fd;

        if (s->shmobj == NULL) {
            fprintf(stderr, "Must specify 'chardev' or 'shm' to ivshmem\n");
        }

        IVSHMEM_DPRINTF("using shm_open (shm object = %s)\n", s->shmobj);

        /* try opening with O_EXCL and if it succeeds zero the memory
         * by truncating to 0 */
        if ((fd = shm_open(s->shmobj, O_CREAT|O_RDWR|O_EXCL,
                        S_IRWXU|S_IRWXG|S_IRWXO)) > 0) {
           /* truncate file to length PCI device's memory */
            if (ftruncate(fd, s->ivshmem_size) != 0) {
                fprintf(stderr, "ivshmem: could not truncate shared file\n");
            }

        } else if ((fd = shm_open(s->shmobj, O_CREAT|O_RDWR,
                        S_IRWXU|S_IRWXG|S_IRWXO)) < 0) {
            fprintf(stderr, "ivshmem: could not open shared file\n");
            exit(-1);

        }

        if (check_shm_size(s, fd) == -1) {
            exit(-1);
        }

        create_shared_memory_BAR(s, fd);

    }

    return 0;
}

static int pci_ivshmem_uninit(PCIDevice *dev)
{
    IVShmemState *s = DO_UPCAST(IVShmemState, dev, dev);

    cpu_unregister_io_memory(s->ivshmem_mmio_io_addr);
    unregister_savevm(&dev->qdev, "ivshmem", s);

    return 0;
}

static PCIDeviceInfo ivshmem_info = {
    .qdev.name  = "ivshmem",
    .qdev.size  = sizeof(IVShmemState),
    .qdev.reset = ivshmem_reset,
    .init       = pci_ivshmem_init,
    .exit       = pci_ivshmem_uninit,
    .vendor_id  = PCI_VENDOR_ID_REDHAT_QUMRANET,
    .device_id  = 0x1110,
    .class_id   = PCI_CLASS_MEMORY_RAM,
    .qdev.props = (Property[]) {
        DEFINE_PROP_CHR("chardev", IVShmemState, server_chr),
        DEFINE_PROP_STRING("size", IVShmemState, sizearg),
        DEFINE_PROP_UINT32("vectors", IVShmemState, vectors, 1),
        DEFINE_PROP_BIT("ioeventfd", IVShmemState, features, IVSHMEM_IOEVENTFD, false),
        DEFINE_PROP_BIT("msi", IVShmemState, features, IVSHMEM_MSI, true),
        DEFINE_PROP_STRING("shm", IVShmemState, shmobj),
        DEFINE_PROP_STRING("role", IVShmemState, role),
        DEFINE_PROP_END_OF_LIST(),
    }
};

static void ivshmem_register_devices(void)
{
    pci_qdev_register(&ivshmem_info);
}

device_init(ivshmem_register_devices)
