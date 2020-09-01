/*
 * virtio-serial Fuzzing Target
 *
 * Copyright Red Hat Inc., 2019
 *
 * Authors:
 *  Alexander Bulekov   <alxndr@bu.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "tests/qtest/libqos/libqtest.h"
#include "tests/qtest/libqos/virtio-scsi.h"
#include "tests/qtest/libqos/virtio.h"
#include "tests/qtest/libqos/virtio-pci.h"
#include "standard-headers/linux/virtio_ids.h"
#include "standard-headers/linux/virtio_pci.h"
#include "standard-headers/linux/virtio_scsi.h"
#include "fuzz.h"
#include "fork_fuzz.h"
#include "qos_fuzz.h"

#define PCI_SLOT                0x02
#define PCI_FN                  0x00
#define QVIRTIO_SCSI_TIMEOUT_US (1 * 1000 * 1000)

#define MAX_NUM_QUEUES 64

/* Based on tests/virtio-scsi-test.c */
typedef struct {
    int num_queues;
    QVirtQueue *vq[MAX_NUM_QUEUES + 2];
} QVirtioSCSIQueues;

static QVirtioSCSIQueues *qvirtio_scsi_init(QVirtioDevice *dev, uint64_t mask)
{
    QVirtioSCSIQueues *vs;
    uint64_t feat;
    int i;

    vs = g_new0(QVirtioSCSIQueues, 1);

    feat = qvirtio_get_features(dev);
    if (mask) {
        feat &= ~QVIRTIO_F_BAD_FEATURE | mask;
    } else {
        feat &= ~(QVIRTIO_F_BAD_FEATURE | (1ull << VIRTIO_RING_F_EVENT_IDX));
    }
    qvirtio_set_features(dev, feat);

    vs->num_queues = qvirtio_config_readl(dev, 0);

    for (i = 0; i < vs->num_queues + 2; i++) {
        vs->vq[i] = qvirtqueue_setup(dev, fuzz_qos_alloc, i);
    }

    qvirtio_set_driver_ok(dev);

    return vs;
}

static void virtio_scsi_fuzz(QTestState *s, QVirtioSCSIQueues* queues,
        const unsigned char *Data, size_t Size)
{
    /*
     * Data is a sequence of random bytes. We split them up into "actions",
     * followed by data:
     * [vqa][dddddddd][vqa][dddd][vqa][dddddddddddd] ...
     * The length of the data is specified by the preceding vqa.length
     */
    typedef struct vq_action {
        uint8_t queue;
        uint8_t length;
        uint8_t write;
        uint8_t next;
        uint8_t kick;
    } vq_action;

    /* Keep track of the free head for each queue we interact with */
    bool vq_touched[MAX_NUM_QUEUES + 2] = {0};
    uint32_t free_head[MAX_NUM_QUEUES + 2];

    QGuestAllocator *t_alloc = fuzz_qos_alloc;

    QVirtioSCSI *scsi = fuzz_qos_obj;
    QVirtioDevice *dev = scsi->vdev;
    QVirtQueue *q;
    vq_action vqa;
    while (Size >= sizeof(vqa)) {
        /* Copy the action, so we can normalize length, queue and flags */
        memcpy(&vqa, Data, sizeof(vqa));

        Data += sizeof(vqa);
        Size -= sizeof(vqa);

        vqa.queue = vqa.queue % queues->num_queues;
        /* Cap length at the number of remaining bytes in data */
        vqa.length = vqa.length >= Size ? Size : vqa.length;
        vqa.write = vqa.write & 1;
        vqa.next = vqa.next & 1;
        vqa.kick = vqa.kick & 1;


        q = queues->vq[vqa.queue];

        /* Copy the data into ram, and place it on the virtqueue */
        uint64_t req_addr = guest_alloc(t_alloc, vqa.length);
        qtest_memwrite(s, req_addr, Data, vqa.length);
        if (vq_touched[vqa.queue] == 0) {
            vq_touched[vqa.queue] = 1;
            free_head[vqa.queue] = qvirtqueue_add(s, q, req_addr, vqa.length,
                    vqa.write, vqa.next);
        } else {
            qvirtqueue_add(s, q, req_addr, vqa.length, vqa.write , vqa.next);
        }

        if (vqa.kick) {
            qvirtqueue_kick(s, dev, q, free_head[vqa.queue]);
            free_head[vqa.queue] = 0;
        }
        Data += vqa.length;
        Size -= vqa.length;
    }
    /* In the end, kick each queue we interacted with */
    for (int i = 0; i < MAX_NUM_QUEUES + 2; i++) {
        if (vq_touched[i]) {
            qvirtqueue_kick(s, dev, queues->vq[i], free_head[i]);
        }
    }
}

static void virtio_scsi_fork_fuzz(QTestState *s,
        const unsigned char *Data, size_t Size)
{
    QVirtioSCSI *scsi = fuzz_qos_obj;
    static QVirtioSCSIQueues *queues;
    if (!queues) {
        queues = qvirtio_scsi_init(scsi->vdev, 0);
    }
    if (fork() == 0) {
        virtio_scsi_fuzz(s, queues, Data, Size);
        flush_events(s);
        _Exit(0);
    } else {
        flush_events(s);
        wait(NULL);
    }
}

static void virtio_scsi_with_flag_fuzz(QTestState *s,
        const unsigned char *Data, size_t Size)
{
    QVirtioSCSI *scsi = fuzz_qos_obj;
    static QVirtioSCSIQueues *queues;

    if (fork() == 0) {
        if (Size >= sizeof(uint64_t)) {
            queues = qvirtio_scsi_init(scsi->vdev, *(uint64_t *)Data);
            virtio_scsi_fuzz(s, queues,
                             Data + sizeof(uint64_t), Size - sizeof(uint64_t));
            flush_events(s);
        }
        _Exit(0);
    } else {
        flush_events(s);
        wait(NULL);
    }
}

static void virtio_scsi_pre_fuzz(QTestState *s)
{
    qos_init_path(s);
    counter_shm_init();
}

static void *virtio_scsi_test_setup(GString *cmd_line, void *arg)
{
    g_string_append(cmd_line,
                    " -drive file=blkdebug::null-co://,"
                    "file.image.read-zeroes=on,"
                    "if=none,id=dr1,format=raw,file.align=4k "
                    "-device scsi-hd,drive=dr1,lun=0,scsi-id=1");
    return arg;
}


static void register_virtio_scsi_fuzz_targets(void)
{
    fuzz_add_qos_target(&(FuzzTarget){
                .name = "virtio-scsi-fuzz",
                .description = "Fuzz the virtio-scsi virtual queues, forking "
                                "for each fuzz run",
                .pre_vm_init = &counter_shm_init,
                .pre_fuzz = &virtio_scsi_pre_fuzz,
                .fuzz = virtio_scsi_fork_fuzz,},
                "virtio-scsi",
                &(QOSGraphTestOptions){.before = virtio_scsi_test_setup}
                );

    fuzz_add_qos_target(&(FuzzTarget){
                .name = "virtio-scsi-flags-fuzz",
                .description = "Fuzz the virtio-scsi virtual queues, forking "
                "for each fuzz run (also fuzzes the virtio flags)",
                .pre_vm_init = &counter_shm_init,
                .pre_fuzz = &virtio_scsi_pre_fuzz,
                .fuzz = virtio_scsi_with_flag_fuzz,},
                "virtio-scsi",
                &(QOSGraphTestOptions){.before = virtio_scsi_test_setup}
                );
}

fuzz_target_init(register_virtio_scsi_fuzz_targets);
