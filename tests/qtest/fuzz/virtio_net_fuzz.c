/*
 * virtio-net Fuzzing Target
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

#include "standard-headers/linux/virtio_config.h"
#include "tests/qtest/libqtest.h"
#include "tests/qtest/libqos/virtio-net.h"
#include "fuzz.h"
#include "fork_fuzz.h"
#include "qos_fuzz.h"


#define QVIRTIO_NET_TIMEOUT_US (30 * 1000 * 1000)
#define QVIRTIO_RX_VQ 0
#define QVIRTIO_TX_VQ 1
#define QVIRTIO_CTRL_VQ 2

static int sockfds[2];
static bool sockfds_initialized;

static void virtio_net_fuzz_multi(QTestState *s,
        const unsigned char *Data, size_t Size, bool check_used)
{
    typedef struct vq_action {
        uint8_t queue;
        uint8_t length;
        uint8_t write;
        uint8_t next;
        uint8_t rx;
    } vq_action;

    uint32_t free_head = 0;

    QGuestAllocator *t_alloc = fuzz_qos_alloc;

    QVirtioNet *net_if = fuzz_qos_obj;
    QVirtioDevice *dev = net_if->vdev;
    QVirtQueue *q;
    vq_action vqa;
    while (Size >= sizeof(vqa)) {
        memcpy(&vqa, Data, sizeof(vqa));
        Data += sizeof(vqa);
        Size -= sizeof(vqa);

        q = net_if->queues[vqa.queue % 3];

        vqa.length = vqa.length >= Size ? Size :  vqa.length;

        /*
         * Only attempt to write incoming packets, when using the socket
         * backend. Otherwise, always place the input on a virtqueue.
         */
        if (vqa.rx && sockfds_initialized) {
            write(sockfds[0], Data, vqa.length);
        } else {
            vqa.rx = 0;
            uint64_t req_addr = guest_alloc(t_alloc, vqa.length);
            /*
             * If checking used ring, ensure that the fuzzer doesn't trigger
             * trivial asserion failure on zero-zied buffer
             */
            qtest_memwrite(s, req_addr, Data, vqa.length);


            free_head = qvirtqueue_add(s, q, req_addr, vqa.length,
                    vqa.write, vqa.next);
            qvirtqueue_add(s, q, req_addr, vqa.length, vqa.write , vqa.next);
            qvirtqueue_kick(s, dev, q, free_head);
        }

        /* Run the main loop */
        qtest_clock_step(s, 100);
        flush_events(s);

        /* Wait on used descriptors */
        if (check_used && !vqa.rx) {
            gint64 start_time = g_get_monotonic_time();
            /*
             * normally, we could just use qvirtio_wait_used_elem, but since we
             * must manually run the main-loop for all the bhs to run, we use
             * this hack with flush_events(), to run the main_loop
             */
            while (!vqa.rx && q != net_if->queues[QVIRTIO_RX_VQ]) {
                uint32_t got_desc_idx;
                /* Input led to a virtio_error */
                if (dev->bus->get_status(dev) & VIRTIO_CONFIG_S_NEEDS_RESET) {
                    break;
                }
                if (dev->bus->get_queue_isr_status(dev, q) &&
                        qvirtqueue_get_buf(s, q, &got_desc_idx, NULL)) {
                    g_assert_cmpint(got_desc_idx, ==, free_head);
                    break;
                }
                g_assert(g_get_monotonic_time() - start_time
                        <= QVIRTIO_NET_TIMEOUT_US);

                /* Run the main loop */
                qtest_clock_step(s, 100);
                flush_events(s);
            }
        }
        Data += vqa.length;
        Size -= vqa.length;
    }
}

static void virtio_net_fork_fuzz(QTestState *s,
        const unsigned char *Data, size_t Size)
{
    if (fork() == 0) {
        virtio_net_fuzz_multi(s, Data, Size, false);
        flush_events(s);
        _Exit(0);
    } else {
        flush_events(s);
        wait(NULL);
    }
}

static void virtio_net_fork_fuzz_check_used(QTestState *s,
        const unsigned char *Data, size_t Size)
{
    if (fork() == 0) {
        virtio_net_fuzz_multi(s, Data, Size, true);
        flush_events(s);
        _Exit(0);
    } else {
        flush_events(s);
        wait(NULL);
    }
}

static void virtio_net_pre_fuzz(QTestState *s)
{
    qos_init_path(s);
    counter_shm_init();
}

static void *virtio_net_test_setup_socket(GString *cmd_line, void *arg)
{
    int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, sockfds);
    g_assert_cmpint(ret, !=, -1);
    fcntl(sockfds[0], F_SETFL, O_NONBLOCK);
    sockfds_initialized = true;
    g_string_append_printf(cmd_line, " -netdev socket,fd=%d,id=hs0 ",
                           sockfds[1]);
    return arg;
}

static void *virtio_net_test_setup_user(GString *cmd_line, void *arg)
{
    g_string_append_printf(cmd_line, " -netdev user,id=hs0 ");
    return arg;
}

static void register_virtio_net_fuzz_targets(void)
{
    fuzz_add_qos_target(&(FuzzTarget){
            .name = "virtio-net-socket",
            .description = "Fuzz the virtio-net virtual queues. Fuzz incoming "
            "traffic using the socket backend",
            .pre_fuzz = &virtio_net_pre_fuzz,
            .fuzz = virtio_net_fork_fuzz,},
            "virtio-net",
            &(QOSGraphTestOptions){.before = virtio_net_test_setup_socket}
            );

    fuzz_add_qos_target(&(FuzzTarget){
            .name = "virtio-net-socket-check-used",
            .description = "Fuzz the virtio-net virtual queues. Wait for the "
            "descriptors to be used. Timeout may indicate improperly handled "
            "input",
            .pre_fuzz = &virtio_net_pre_fuzz,
            .fuzz = virtio_net_fork_fuzz_check_used,},
            "virtio-net",
            &(QOSGraphTestOptions){.before = virtio_net_test_setup_socket}
            );
    fuzz_add_qos_target(&(FuzzTarget){
            .name = "virtio-net-slirp",
            .description = "Fuzz the virtio-net virtual queues with the slirp "
            " backend. Warning: May result in network traffic emitted from the "
            " process. Run in an isolated network environment.",
            .pre_fuzz = &virtio_net_pre_fuzz,
            .fuzz = virtio_net_fork_fuzz,},
            "virtio-net",
            &(QOSGraphTestOptions){.before = virtio_net_test_setup_user}
            );
}

fuzz_target_init(register_virtio_net_fuzz_targets);
