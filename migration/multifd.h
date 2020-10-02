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
void multifd_send_sync_main(QEMUFile *f);
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
    uint32_t pages_used;
    /* size of the next packet that contains pages */
    uint32_t next_packet_size;
    uint64_t packet_num;
    uint64_t unused[4];    /* Reserved for future use */
    char ramblock[256];
    uint64_t offset[];
} __attribute__((packed)) MultiFDPacket_t;

typedef struct {
    /* number of used pages */
    uint32_t used;
    /* number of allocated pages */
    uint32_t allocated;
    /* global number of generated multifd packets */
    uint64_t packet_num;
    /* offset of each page */
    ram_addr_t *offset;
    /* pointer to each page */
    struct iovec *iov;
    RAMBlock *block;
} MultiFDPages_t;

typedef struct {
    /* this fields are not changed once the thread is created */
    /* channel number */
    uint8_t id;
    /* channel thread name */
    char *name;
    /* tls hostname */
    char *tls_hostname;
    /* channel thread id */
    QemuThread thread;
    /* communication channel */
    QIOChannel *c;
    /* sem where to wait for more work */
    QemuSemaphore sem;
    /* this mutex protects the following parameters */
    QemuMutex mutex;
    /* is this channel thread running */
    bool running;
    /* should this thread finish */
    bool quit;
    /* thread has work to do */
    int pending_job;
    /* array of pages to sent */
    MultiFDPages_t *pages;
    /* packet allocated len */
    uint32_t packet_len;
    /* pointer to the packet */
    MultiFDPacket_t *packet;
    /* multifd flags for each packet */
    uint32_t flags;
    /* size of the next packet that contains pages */
    uint32_t next_packet_size;
    /* global number of generated multifd packets */
    uint64_t packet_num;
    /* thread local variables */
    /* packets sent through this channel */
    uint64_t num_packets;
    /* pages sent through this channel */
    uint64_t num_pages;
    /* syncs main thread and channels */
    QemuSemaphore sem_sync;
    /* used for compression methods */
    void *data;
}  MultiFDSendParams;

typedef struct {
    /* this fields are not changed once the thread is created */
    /* channel number */
    uint8_t id;
    /* channel thread name */
    char *name;
    /* channel thread id */
    QemuThread thread;
    /* communication channel */
    QIOChannel *c;
    /* this mutex protects the following parameters */
    QemuMutex mutex;
    /* is this channel thread running */
    bool running;
    /* should this thread finish */
    bool quit;
    /* array of pages to receive */
    MultiFDPages_t *pages;
    /* packet allocated len */
    uint32_t packet_len;
    /* pointer to the packet */
    MultiFDPacket_t *packet;
    /* multifd flags for each packet */
    uint32_t flags;
    /* global number of generated multifd packets */
    uint64_t packet_num;
    /* thread local variables */
    /* size of the next packet that contains pages */
    uint32_t next_packet_size;
    /* packets sent through this channel */
    uint64_t num_packets;
    /* pages sent through this channel */
    uint64_t num_pages;
    /* syncs main thread and channels */
    QemuSemaphore sem_sync;
    /* used for de-compression methods */
    void *data;
} MultiFDRecvParams;

typedef struct {
    /* Setup for sending side */
    int (*send_setup)(MultiFDSendParams *p, Error **errp);
    /* Cleanup for sending side */
    void (*send_cleanup)(MultiFDSendParams *p, Error **errp);
    /* Prepare the send packet */
    int (*send_prepare)(MultiFDSendParams *p, uint32_t used, Error **errp);
    /* Write the send packet */
    int (*send_write)(MultiFDSendParams *p, uint32_t used, Error **errp);
    /* Setup for receiving side */
    int (*recv_setup)(MultiFDRecvParams *p, Error **errp);
    /* Cleanup for receiving side */
    void (*recv_cleanup)(MultiFDRecvParams *p);
    /* Read all pages */
    int (*recv_pages)(MultiFDRecvParams *p, uint32_t used, Error **errp);
} MultiFDMethods;

void multifd_register_ops(int method, MultiFDMethods *ops);

#endif

