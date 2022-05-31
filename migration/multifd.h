/*
 * Multifd common functions
 *
 * Copyright (c) 2019-2020 Red Hat Inc
 *
 * Authors:
 *  Juan Quintela <quintela@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_MIGRATION_MULTIFD_H
#define QEMU_MIGRATION_MULTIFD_H

int multifd_save_setup(Error **errp);
void multifd_save_cleanup(void);
int multifd_load_setup(Error **errp);
int multifd_load_cleanup(Error **errp);
bool multifd_recv_all_channels_created(void);
bool multifd_recv_new_channel(QIOChannel *ioc, Error **errp);
void multifd_recv_sync_main(void);
int multifd_send_sync_main(QEMUFile *f);
int multifd_queue_page(QEMUFile *f, RAMBlock *block, ram_addr_t offset);

/* Multifd Compression flags */
#define MULTIFD_FLAG_SYNC (1 << 0)

/* We reserve 3 bits for compression methods */
#define MULTIFD_FLAG_COMPRESSION_MASK (7 << 1)
/* we need to be compatible. Before compression value was 0 */
#define MULTIFD_FLAG_NOCOMP (0 << 1)
#define MULTIFD_FLAG_ZLIB (1 << 1)
#define MULTIFD_FLAG_ZSTD (2 << 1)

/* This value needs to be a multiple of qemu_target_page_size() */
#define MULTIFD_PACKET_SIZE (512 * 1024)

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t flags;
    /* maximum number of allocated pages */
    uint32_t pages_alloc;
    /* non zero pages */
    uint32_t normal_pages;
    /* size of the next packet that contains pages */
    uint32_t next_packet_size;
    uint64_t packet_num;
    uint64_t unused[4];    /* Reserved for future use */
    char ramblock[256];
    uint64_t offset[];
} __attribute__((packed)) MultiFDPacket_t;

typedef struct {
    /* number of used pages */
    uint32_t num;
    /* number of allocated pages */
    uint32_t allocated;
    /* global number of generated multifd packets */
    uint64_t packet_num;
    /* offset of each page */
    ram_addr_t *offset;
    RAMBlock *block;
} MultiFDPages_t;

typedef struct {
    /* Fields are only written at creating/deletion time */
    /* No lock required for them, they are read only */

    /* channel number */
    uint8_t id;
    /* channel thread name */
    char *name;
    /* channel thread id */
    QemuThread thread;
    /* communication channel */
    QIOChannel *c;
    /* is the yank function registered */
    bool registered_yank;
    /* packet allocated len */
    uint32_t packet_len;
    /* multifd flags for sending ram */
    int write_flags;

    /* sem where to wait for more work */
    QemuSemaphore sem;
    /* syncs main thread and channels */
    QemuSemaphore sem_sync;

    /* this mutex protects the following parameters */
    QemuMutex mutex;
    /* is this channel thread running */
    bool running;
    /* should this thread finish */
    bool quit;
    /* multifd flags for each packet */
    uint32_t flags;
    /* global number of generated multifd packets */
    uint64_t packet_num;
    /* thread has work to do */
    int pending_job;
    /* array of pages to sent.
     * The owner of 'pages' depends of 'pending_job' value:
     * pending_job == 0 -> migration_thread can use it.
     * pending_job != 0 -> multifd_channel can use it.
     */
    MultiFDPages_t *pages;

    /* thread local variables. No locking required */

    /* pointer to the packet */
    MultiFDPacket_t *packet;
    /* size of the next packet that contains pages */
    uint32_t next_packet_size;
    /* packets sent through this channel */
    uint64_t num_packets;
    /* non zero pages sent through this channel */
    uint64_t total_normal_pages;
    /* buffers to send */
    struct iovec *iov;
    /* number of iovs used */
    uint32_t iovs_num;
    /* Pages that are not zero */
    ram_addr_t *normal;
    /* num of non zero pages */
    uint32_t normal_num;
    /* used for compression methods */
    void *data;
}  MultiFDSendParams;

typedef struct {
    /* Fields are only written at creating/deletion time */
    /* No lock required for them, they are read only */

    /* channel number */
    uint8_t id;
    /* channel thread name */
    char *name;
    /* channel thread id */
    QemuThread thread;
    /* communication channel */
    QIOChannel *c;
    /* packet allocated len */
    uint32_t packet_len;

    /* syncs main thread and channels */
    QemuSemaphore sem_sync;

    /* this mutex protects the following parameters */
    QemuMutex mutex;
    /* is this channel thread running */
    bool running;
    /* should this thread finish */
    bool quit;
    /* multifd flags for each packet */
    uint32_t flags;
    /* global number of generated multifd packets */
    uint64_t packet_num;

    /* thread local variables. No locking required */

    /* pointer to the packet */
    MultiFDPacket_t *packet;
    /* size of the next packet that contains pages */
    uint32_t next_packet_size;
    /* packets sent through this channel */
    uint64_t num_packets;
    /* ramblock host address */
    uint8_t *host;
    /* non zero pages recv through this channel */
    uint64_t total_normal_pages;
    /* buffers to recv */
    struct iovec *iov;
    /* Pages that are not zero */
    ram_addr_t *normal;
    /* num of non zero pages */
    uint32_t normal_num;
    /* used for de-compression methods */
    void *data;
} MultiFDRecvParams;

typedef struct {
    /* Setup for sending side */
    int (*send_setup)(MultiFDSendParams *p, Error **errp);
    /* Cleanup for sending side */
    void (*send_cleanup)(MultiFDSendParams *p, Error **errp);
    /* Prepare the send packet */
    int (*send_prepare)(MultiFDSendParams *p, Error **errp);
    /* Setup for receiving side */
    int (*recv_setup)(MultiFDRecvParams *p, Error **errp);
    /* Cleanup for receiving side */
    void (*recv_cleanup)(MultiFDRecvParams *p);
    /* Read all pages */
    int (*recv_pages)(MultiFDRecvParams *p, Error **errp);
} MultiFDMethods;

void multifd_register_ops(int method, MultiFDMethods *ops);

#endif

