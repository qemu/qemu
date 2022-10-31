/*
 * virtio-blk Fuzzing Target
 *
 * Copyright Red Hat Inc., 2020
 *
 * Based on virtio-scsi-fuzz target.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "tests/qtest/libqtest.h"
#include "tests/qtest/libqos/virtio-blk.h"
#include "tests/qtest/libqos/virtio.h"
#include "tests/qtest/libqos/virtio-pci.h"
#include "standard-headers/linux/virtio_ids.h"
#include "standard-headers/linux/virtio_pci.h"
#include "standard-headers/linux/virtio_blk.h"
#include "fuzz.h"
#include "fork_fuzz.h"
#include "qos_fuzz.h"

#define TEST_IMAGE_SIZE         (64 * 1024 * 1024)
#define PCI_SLOT                0x02
#define PCI_FN                  0x00

#define MAX_NUM_QUEUES 64

/* Based on tests/qtest/virtio-blk-test.c. */
typedef struct {
    int num_queues;
    QVirtQueue *vq[MAX_NUM_QUEUES + 2];
} QVirtioBlkQueues;

static QVirtioBlkQueues *qvirtio_blk_init(QVirtioDevice *dev, uint64_t mask)
{
    QVirtioBlkQueues *vs;
    uint64_t features;

    vs = g_new0(QVirtioBlkQueues, 1);

    features = qvirtio_get_features(dev);
    if (!mask) {
        mask = ~((1u << VIRTIO_RING_F_INDIRECT_DESC) |
                (1u << VIRTIO_RING_F_EVENT_IDX) |
                (1u << VIRTIO_BLK_F_SCSI));
    }
    mask |= ~QVIRTIO_F_BAD_FEATURE;
    features &= mask;
    qvirtio_set_features(dev, features);

    vs->num_queues = 1;
    vs->vq[0] = qvirtqueue_setup(dev, fuzz_qos_alloc, 0);

    qvirtio_set_driver_ok(dev);

    return vs;
}

static void virtio_blk_fuzz(QTestState *s, QVirtioBlkQueues* queues,
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

    QVirtioBlk *blk = fuzz_qos_obj;
    QVirtioDevice *dev = blk->vdev;
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

static void virtio_blk_fork_fuzz(QTestState *s,
        const unsigned char *Data, size_t Size)
{
    QVirtioBlk *blk = fuzz_qos_obj;
    static QVirtioBlkQueues *queues;
    if (!queues) {
        queues = qvirtio_blk_init(blk->vdev, 0);
    }
    if (fork() == 0) {
        virtio_blk_fuzz(s, queues, Data, Size);
        flush_events(s);
        _Exit(0);
    } else {
        flush_events(s);
        wait(NULL);
    }
}

static void virtio_blk_with_flag_fuzz(QTestState *s,
        const unsigned char *Data, size_t Size)
{
    QVirtioBlk *blk = fuzz_qos_obj;
    static QVirtioBlkQueues *queues;

    if (fork() == 0) {
        if (Size >= sizeof(uint64_t)) {
            queues = qvirtio_blk_init(blk->vdev, *(uint64_t *)Data);
            virtio_blk_fuzz(s, queues,
                             Data + sizeof(uint64_t), Size - sizeof(uint64_t));
            flush_events(s);
        }
        _Exit(0);
    } else {
        flush_events(s);
        wait(NULL);
    }
}

static void virtio_blk_pre_fuzz(QTestState *s)
{
    qos_init_path(s);
    counter_shm_init();
}

static void drive_destroy(void *path)
{
    unlink(path);
    g_free(path);
}

static char *drive_create(void)
{
    int fd, ret;
    char *t_path;

    /* Create a temporary raw image */
    fd = g_file_open_tmp("qtest.XXXXXX", &t_path, NULL);
    g_assert_cmpint(fd, >=, 0);
    ret = ftruncate(fd, TEST_IMAGE_SIZE);
    g_assert_cmpint(ret, ==, 0);
    close(fd);

    g_test_queue_destroy(drive_destroy, t_path);
    return t_path;
}

static void *virtio_blk_test_setup(GString *cmd_line, void *arg)
{
    char *tmp_path = drive_create();

    g_string_append_printf(cmd_line,
                           " -drive if=none,id=drive0,file=%s,"
                           "format=raw,auto-read-only=off ",
                           tmp_path);

    return arg;
}

static void register_virtio_blk_fuzz_targets(void)
{
    fuzz_add_qos_target(&(FuzzTarget){
                .name = "virtio-blk-fuzz",
                .description = "Fuzz the virtio-blk virtual queues, forking "
                                "for each fuzz run",
                .pre_vm_init = &counter_shm_init,
                .pre_fuzz = &virtio_blk_pre_fuzz,
                .fuzz = virtio_blk_fork_fuzz,},
                "virtio-blk",
                &(QOSGraphTestOptions){.before = virtio_blk_test_setup}
                );

    fuzz_add_qos_target(&(FuzzTarget){
                .name = "virtio-blk-flags-fuzz",
                .description = "Fuzz the virtio-blk virtual queues, forking "
                "for each fuzz run (also fuzzes the virtio flags)",
                .pre_vm_init = &counter_shm_init,
                .pre_fuzz = &virtio_blk_pre_fuzz,
                .fuzz = virtio_blk_with_flag_fuzz,},
                "virtio-blk",
                &(QOSGraphTestOptions){.before = virtio_blk_test_setup}
                );
}

fuzz_target_init(register_virtio_blk_fuzz_targets);
