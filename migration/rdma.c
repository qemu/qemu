/*
 * RDMA protocol and interfaces
 *
 * Copyright IBM, Corp. 2010-2013
 * Copyright Red Hat, Inc. 2015-2016
 *
 * Authors:
 *  Michael R. Hines <mrhines@us.ibm.com>
 *  Jiuxing Liu <jl@us.ibm.com>
 *  Daniel P. Berrange <berrange@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/cutils.h"
#include "exec/target_page.h"
#include "rdma.h"
#include "migration.h"
#include "migration-stats.h"
#include "qemu-file.h"
#include "ram.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qemu/rcu.h"
#include "qemu/sockets.h"
#include "qemu/bitmap.h"
#include "qemu/coroutine.h"
#include "exec/memory.h"
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <rdma/rdma_cma.h>
#include "trace.h"
#include "qom/object.h"
#include "options.h"
#include <poll.h>

#define RDMA_RESOLVE_TIMEOUT_MS 10000

/* Do not merge data if larger than this. */
#define RDMA_MERGE_MAX (2 * 1024 * 1024)
#define RDMA_SIGNALED_SEND_MAX (RDMA_MERGE_MAX / 4096)

#define RDMA_REG_CHUNK_SHIFT 20 /* 1 MB */

/*
 * This is only for non-live state being migrated.
 * Instead of RDMA_WRITE messages, we use RDMA_SEND
 * messages for that state, which requires a different
 * delivery design than main memory.
 */
#define RDMA_SEND_INCREMENT 32768

/*
 * Maximum size infiniband SEND message
 */
#define RDMA_CONTROL_MAX_BUFFER (512 * 1024)
#define RDMA_CONTROL_MAX_COMMANDS_PER_MESSAGE 4096

#define RDMA_CONTROL_VERSION_CURRENT 1
/*
 * Capabilities for negotiation.
 */
#define RDMA_CAPABILITY_PIN_ALL 0x01

/*
 * Add the other flags above to this list of known capabilities
 * as they are introduced.
 */
static uint32_t known_capabilities = RDMA_CAPABILITY_PIN_ALL;

/*
 * A work request ID is 64-bits and we split up these bits
 * into 3 parts:
 *
 * bits 0-15 : type of control message, 2^16
 * bits 16-29: ram block index, 2^14
 * bits 30-63: ram block chunk number, 2^34
 *
 * The last two bit ranges are only used for RDMA writes,
 * in order to track their completion and potentially
 * also track unregistration status of the message.
 */
#define RDMA_WRID_TYPE_SHIFT  0UL
#define RDMA_WRID_BLOCK_SHIFT 16UL
#define RDMA_WRID_CHUNK_SHIFT 30UL

#define RDMA_WRID_TYPE_MASK \
    ((1UL << RDMA_WRID_BLOCK_SHIFT) - 1UL)

#define RDMA_WRID_BLOCK_MASK \
    (~RDMA_WRID_TYPE_MASK & ((1UL << RDMA_WRID_CHUNK_SHIFT) - 1UL))

#define RDMA_WRID_CHUNK_MASK (~RDMA_WRID_BLOCK_MASK & ~RDMA_WRID_TYPE_MASK)

/*
 * RDMA migration protocol:
 * 1. RDMA Writes (data messages, i.e. RAM)
 * 2. IB Send/Recv (control channel messages)
 */
enum {
    RDMA_WRID_NONE = 0,
    RDMA_WRID_RDMA_WRITE = 1,
    RDMA_WRID_SEND_CONTROL = 2000,
    RDMA_WRID_RECV_CONTROL = 4000,
};

/*
 * Work request IDs for IB SEND messages only (not RDMA writes).
 * This is used by the migration protocol to transmit
 * control messages (such as device state and registration commands)
 *
 * We could use more WRs, but we have enough for now.
 */
enum {
    RDMA_WRID_READY = 0,
    RDMA_WRID_DATA,
    RDMA_WRID_CONTROL,
    RDMA_WRID_MAX,
};

/*
 * SEND/RECV IB Control Messages.
 */
enum {
    RDMA_CONTROL_NONE = 0,
    RDMA_CONTROL_ERROR,
    RDMA_CONTROL_READY,               /* ready to receive */
    RDMA_CONTROL_QEMU_FILE,           /* QEMUFile-transmitted bytes */
    RDMA_CONTROL_RAM_BLOCKS_REQUEST,  /* RAMBlock synchronization */
    RDMA_CONTROL_RAM_BLOCKS_RESULT,   /* RAMBlock synchronization */
    RDMA_CONTROL_COMPRESS,            /* page contains repeat values */
    RDMA_CONTROL_REGISTER_REQUEST,    /* dynamic page registration */
    RDMA_CONTROL_REGISTER_RESULT,     /* key to use after registration */
    RDMA_CONTROL_REGISTER_FINISHED,   /* current iteration finished */
    RDMA_CONTROL_UNREGISTER_REQUEST,  /* dynamic UN-registration */
    RDMA_CONTROL_UNREGISTER_FINISHED, /* unpinning finished */
};


/*
 * Memory and MR structures used to represent an IB Send/Recv work request.
 * This is *not* used for RDMA writes, only IB Send/Recv.
 */
typedef struct {
    uint8_t  control[RDMA_CONTROL_MAX_BUFFER]; /* actual buffer to register */
    struct   ibv_mr *control_mr;               /* registration metadata */
    size_t   control_len;                      /* length of the message */
    uint8_t *control_curr;                     /* start of unconsumed bytes */
} RDMAWorkRequestData;

/*
 * Negotiate RDMA capabilities during connection-setup time.
 */
typedef struct {
    uint32_t version;
    uint32_t flags;
} RDMACapabilities;

static void caps_to_network(RDMACapabilities *cap)
{
    cap->version = htonl(cap->version);
    cap->flags = htonl(cap->flags);
}

static void network_to_caps(RDMACapabilities *cap)
{
    cap->version = ntohl(cap->version);
    cap->flags = ntohl(cap->flags);
}

/*
 * Representation of a RAMBlock from an RDMA perspective.
 * This is not transmitted, only local.
 * This and subsequent structures cannot be linked lists
 * because we're using a single IB message to transmit
 * the information. It's small anyway, so a list is overkill.
 */
typedef struct RDMALocalBlock {
    char          *block_name;
    uint8_t       *local_host_addr; /* local virtual address */
    uint64_t       remote_host_addr; /* remote virtual address */
    uint64_t       offset;
    uint64_t       length;
    struct         ibv_mr **pmr;    /* MRs for chunk-level registration */
    struct         ibv_mr *mr;      /* MR for non-chunk-level registration */
    uint32_t      *remote_keys;     /* rkeys for chunk-level registration */
    uint32_t       remote_rkey;     /* rkeys for non-chunk-level registration */
    int            index;           /* which block are we */
    unsigned int   src_index;       /* (Only used on dest) */
    bool           is_ram_block;
    int            nb_chunks;
    unsigned long *transit_bitmap;
    unsigned long *unregister_bitmap;
} RDMALocalBlock;

/*
 * Also represents a RAMblock, but only on the dest.
 * This gets transmitted by the dest during connection-time
 * to the source VM and then is used to populate the
 * corresponding RDMALocalBlock with
 * the information needed to perform the actual RDMA.
 */
typedef struct QEMU_PACKED RDMADestBlock {
    uint64_t remote_host_addr;
    uint64_t offset;
    uint64_t length;
    uint32_t remote_rkey;
    uint32_t padding;
} RDMADestBlock;

static const char *control_desc(unsigned int rdma_control)
{
    static const char *strs[] = {
        [RDMA_CONTROL_NONE] = "NONE",
        [RDMA_CONTROL_ERROR] = "ERROR",
        [RDMA_CONTROL_READY] = "READY",
        [RDMA_CONTROL_QEMU_FILE] = "QEMU FILE",
        [RDMA_CONTROL_RAM_BLOCKS_REQUEST] = "RAM BLOCKS REQUEST",
        [RDMA_CONTROL_RAM_BLOCKS_RESULT] = "RAM BLOCKS RESULT",
        [RDMA_CONTROL_COMPRESS] = "COMPRESS",
        [RDMA_CONTROL_REGISTER_REQUEST] = "REGISTER REQUEST",
        [RDMA_CONTROL_REGISTER_RESULT] = "REGISTER RESULT",
        [RDMA_CONTROL_REGISTER_FINISHED] = "REGISTER FINISHED",
        [RDMA_CONTROL_UNREGISTER_REQUEST] = "UNREGISTER REQUEST",
        [RDMA_CONTROL_UNREGISTER_FINISHED] = "UNREGISTER FINISHED",
    };

    if (rdma_control > RDMA_CONTROL_UNREGISTER_FINISHED) {
        return "??BAD CONTROL VALUE??";
    }

    return strs[rdma_control];
}

static uint64_t htonll(uint64_t v)
{
    union { uint32_t lv[2]; uint64_t llv; } u;
    u.lv[0] = htonl(v >> 32);
    u.lv[1] = htonl(v & 0xFFFFFFFFULL);
    return u.llv;
}

static uint64_t ntohll(uint64_t v)
{
    union { uint32_t lv[2]; uint64_t llv; } u;
    u.llv = v;
    return ((uint64_t)ntohl(u.lv[0]) << 32) | (uint64_t) ntohl(u.lv[1]);
}

static void dest_block_to_network(RDMADestBlock *db)
{
    db->remote_host_addr = htonll(db->remote_host_addr);
    db->offset = htonll(db->offset);
    db->length = htonll(db->length);
    db->remote_rkey = htonl(db->remote_rkey);
}

static void network_to_dest_block(RDMADestBlock *db)
{
    db->remote_host_addr = ntohll(db->remote_host_addr);
    db->offset = ntohll(db->offset);
    db->length = ntohll(db->length);
    db->remote_rkey = ntohl(db->remote_rkey);
}

/*
 * Virtual address of the above structures used for transmitting
 * the RAMBlock descriptions at connection-time.
 * This structure is *not* transmitted.
 */
typedef struct RDMALocalBlocks {
    int nb_blocks;
    bool     init;             /* main memory init complete */
    RDMALocalBlock *block;
} RDMALocalBlocks;

/*
 * Main data structure for RDMA state.
 * While there is only one copy of this structure being allocated right now,
 * this is the place where one would start if you wanted to consider
 * having more than one RDMA connection open at the same time.
 */
typedef struct RDMAContext {
    char *host;
    int port;

    RDMAWorkRequestData wr_data[RDMA_WRID_MAX];

    /*
     * This is used by *_exchange_send() to figure out whether or not
     * the initial "READY" message has already been received or not.
     * This is because other functions may potentially poll() and detect
     * the READY message before send() does, in which case we need to
     * know if it completed.
     */
    int control_ready_expected;

    /* number of outstanding writes */
    int nb_sent;

    /* store info about current buffer so that we can
       merge it with future sends */
    uint64_t current_addr;
    uint64_t current_length;
    /* index of ram block the current buffer belongs to */
    int current_index;
    /* index of the chunk in the current ram block */
    int current_chunk;

    bool pin_all;

    /*
     * infiniband-specific variables for opening the device
     * and maintaining connection state and so forth.
     *
     * cm_id also has ibv_context, rdma_event_channel, and ibv_qp in
     * cm_id->verbs, cm_id->channel, and cm_id->qp.
     */
    struct rdma_cm_id *cm_id;               /* connection manager ID */
    struct rdma_cm_id *listen_id;
    bool connected;

    struct ibv_context          *verbs;
    struct rdma_event_channel   *channel;
    struct ibv_qp *qp;                      /* queue pair */
    struct ibv_comp_channel *recv_comp_channel;  /* recv completion channel */
    struct ibv_comp_channel *send_comp_channel;  /* send completion channel */
    struct ibv_pd *pd;                      /* protection domain */
    struct ibv_cq *recv_cq;                 /* recvieve completion queue */
    struct ibv_cq *send_cq;                 /* send completion queue */

    /*
     * If a previous write failed (perhaps because of a failed
     * memory registration, then do not attempt any future work
     * and remember the error state.
     */
    bool errored;
    bool error_reported;
    bool received_error;

    /*
     * Description of ram blocks used throughout the code.
     */
    RDMALocalBlocks local_ram_blocks;
    RDMADestBlock  *dest_blocks;

    /* Index of the next RAMBlock received during block registration */
    unsigned int    next_src_index;

    /*
     * Migration on *destination* started.
     * Then use coroutine yield function.
     * Source runs in a thread, so we don't care.
     */
    int migration_started_on_destination;

    int total_registrations;
    int total_writes;

    int unregister_current, unregister_next;
    uint64_t unregistrations[RDMA_SIGNALED_SEND_MAX];

    GHashTable *blockmap;

    /* the RDMAContext for return path */
    struct RDMAContext *return_path;
    bool is_return_path;
} RDMAContext;

#define TYPE_QIO_CHANNEL_RDMA "qio-channel-rdma"
OBJECT_DECLARE_SIMPLE_TYPE(QIOChannelRDMA, QIO_CHANNEL_RDMA)



struct QIOChannelRDMA {
    QIOChannel parent;
    RDMAContext *rdmain;
    RDMAContext *rdmaout;
    QEMUFile *file;
    bool blocking; /* XXX we don't actually honour this yet */
};

/*
 * Main structure for IB Send/Recv control messages.
 * This gets prepended at the beginning of every Send/Recv.
 */
typedef struct QEMU_PACKED {
    uint32_t len;     /* Total length of data portion */
    uint32_t type;    /* which control command to perform */
    uint32_t repeat;  /* number of commands in data portion of same type */
    uint32_t padding;
} RDMAControlHeader;

static void control_to_network(RDMAControlHeader *control)
{
    control->type = htonl(control->type);
    control->len = htonl(control->len);
    control->repeat = htonl(control->repeat);
}

static void network_to_control(RDMAControlHeader *control)
{
    control->type = ntohl(control->type);
    control->len = ntohl(control->len);
    control->repeat = ntohl(control->repeat);
}

/*
 * Register a single Chunk.
 * Information sent by the source VM to inform the dest
 * to register an single chunk of memory before we can perform
 * the actual RDMA operation.
 */
typedef struct QEMU_PACKED {
    union QEMU_PACKED {
        uint64_t current_addr;  /* offset into the ram_addr_t space */
        uint64_t chunk;         /* chunk to lookup if unregistering */
    } key;
    uint32_t current_index; /* which ramblock the chunk belongs to */
    uint32_t padding;
    uint64_t chunks;            /* how many sequential chunks to register */
} RDMARegister;

static bool rdma_errored(RDMAContext *rdma)
{
    if (rdma->errored && !rdma->error_reported) {
        error_report("RDMA is in an error state waiting migration"
                     " to abort!");
        rdma->error_reported = true;
    }
    return rdma->errored;
}

static void register_to_network(RDMAContext *rdma, RDMARegister *reg)
{
    RDMALocalBlock *local_block;
    local_block  = &rdma->local_ram_blocks.block[reg->current_index];

    if (local_block->is_ram_block) {
        /*
         * current_addr as passed in is an address in the local ram_addr_t
         * space, we need to translate this for the destination
         */
        reg->key.current_addr -= local_block->offset;
        reg->key.current_addr += rdma->dest_blocks[reg->current_index].offset;
    }
    reg->key.current_addr = htonll(reg->key.current_addr);
    reg->current_index = htonl(reg->current_index);
    reg->chunks = htonll(reg->chunks);
}

static void network_to_register(RDMARegister *reg)
{
    reg->key.current_addr = ntohll(reg->key.current_addr);
    reg->current_index = ntohl(reg->current_index);
    reg->chunks = ntohll(reg->chunks);
}

typedef struct QEMU_PACKED {
    uint32_t value;     /* if zero, we will madvise() */
    uint32_t block_idx; /* which ram block index */
    uint64_t offset;    /* Address in remote ram_addr_t space */
    uint64_t length;    /* length of the chunk */
} RDMACompress;

static void compress_to_network(RDMAContext *rdma, RDMACompress *comp)
{
    comp->value = htonl(comp->value);
    /*
     * comp->offset as passed in is an address in the local ram_addr_t
     * space, we need to translate this for the destination
     */
    comp->offset -= rdma->local_ram_blocks.block[comp->block_idx].offset;
    comp->offset += rdma->dest_blocks[comp->block_idx].offset;
    comp->block_idx = htonl(comp->block_idx);
    comp->offset = htonll(comp->offset);
    comp->length = htonll(comp->length);
}

static void network_to_compress(RDMACompress *comp)
{
    comp->value = ntohl(comp->value);
    comp->block_idx = ntohl(comp->block_idx);
    comp->offset = ntohll(comp->offset);
    comp->length = ntohll(comp->length);
}

/*
 * The result of the dest's memory registration produces an "rkey"
 * which the source VM must reference in order to perform
 * the RDMA operation.
 */
typedef struct QEMU_PACKED {
    uint32_t rkey;
    uint32_t padding;
    uint64_t host_addr;
} RDMARegisterResult;

static void result_to_network(RDMARegisterResult *result)
{
    result->rkey = htonl(result->rkey);
    result->host_addr = htonll(result->host_addr);
};

static void network_to_result(RDMARegisterResult *result)
{
    result->rkey = ntohl(result->rkey);
    result->host_addr = ntohll(result->host_addr);
};

static int qemu_rdma_exchange_send(RDMAContext *rdma, RDMAControlHeader *head,
                                   uint8_t *data, RDMAControlHeader *resp,
                                   int *resp_idx,
                                   int (*callback)(RDMAContext *rdma,
                                                   Error **errp),
                                   Error **errp);

static inline uint64_t ram_chunk_index(const uint8_t *start,
                                       const uint8_t *host)
{
    return ((uintptr_t) host - (uintptr_t) start) >> RDMA_REG_CHUNK_SHIFT;
}

static inline uint8_t *ram_chunk_start(const RDMALocalBlock *rdma_ram_block,
                                       uint64_t i)
{
    return (uint8_t *)(uintptr_t)(rdma_ram_block->local_host_addr +
                                  (i << RDMA_REG_CHUNK_SHIFT));
}

static inline uint8_t *ram_chunk_end(const RDMALocalBlock *rdma_ram_block,
                                     uint64_t i)
{
    uint8_t *result = ram_chunk_start(rdma_ram_block, i) +
                                         (1UL << RDMA_REG_CHUNK_SHIFT);

    if (result > (rdma_ram_block->local_host_addr + rdma_ram_block->length)) {
        result = rdma_ram_block->local_host_addr + rdma_ram_block->length;
    }

    return result;
}

static void rdma_add_block(RDMAContext *rdma, const char *block_name,
                           void *host_addr,
                           ram_addr_t block_offset, uint64_t length)
{
    RDMALocalBlocks *local = &rdma->local_ram_blocks;
    RDMALocalBlock *block;
    RDMALocalBlock *old = local->block;

    local->block = g_new0(RDMALocalBlock, local->nb_blocks + 1);

    if (local->nb_blocks) {
        if (rdma->blockmap) {
            for (int x = 0; x < local->nb_blocks; x++) {
                g_hash_table_remove(rdma->blockmap,
                                    (void *)(uintptr_t)old[x].offset);
                g_hash_table_insert(rdma->blockmap,
                                    (void *)(uintptr_t)old[x].offset,
                                    &local->block[x]);
            }
        }
        memcpy(local->block, old, sizeof(RDMALocalBlock) * local->nb_blocks);
        g_free(old);
    }

    block = &local->block[local->nb_blocks];

    block->block_name = g_strdup(block_name);
    block->local_host_addr = host_addr;
    block->offset = block_offset;
    block->length = length;
    block->index = local->nb_blocks;
    block->src_index = ~0U; /* Filled in by the receipt of the block list */
    block->nb_chunks = ram_chunk_index(host_addr, host_addr + length) + 1UL;
    block->transit_bitmap = bitmap_new(block->nb_chunks);
    bitmap_clear(block->transit_bitmap, 0, block->nb_chunks);
    block->unregister_bitmap = bitmap_new(block->nb_chunks);
    bitmap_clear(block->unregister_bitmap, 0, block->nb_chunks);
    block->remote_keys = g_new0(uint32_t, block->nb_chunks);

    block->is_ram_block = local->init ? false : true;

    if (rdma->blockmap) {
        g_hash_table_insert(rdma->blockmap, (void *)(uintptr_t)block_offset, block);
    }

    trace_rdma_add_block(block_name, local->nb_blocks,
                         (uintptr_t) block->local_host_addr,
                         block->offset, block->length,
                         (uintptr_t) (block->local_host_addr + block->length),
                         BITS_TO_LONGS(block->nb_chunks) *
                             sizeof(unsigned long) * 8,
                         block->nb_chunks);

    local->nb_blocks++;
}

/*
 * Memory regions need to be registered with the device and queue pairs setup
 * in advanced before the migration starts. This tells us where the RAM blocks
 * are so that we can register them individually.
 */
static int qemu_rdma_init_one_block(RAMBlock *rb, void *opaque)
{
    const char *block_name = qemu_ram_get_idstr(rb);
    void *host_addr = qemu_ram_get_host_addr(rb);
    ram_addr_t block_offset = qemu_ram_get_offset(rb);
    ram_addr_t length = qemu_ram_get_used_length(rb);
    rdma_add_block(opaque, block_name, host_addr, block_offset, length);
    return 0;
}

/*
 * Identify the RAMBlocks and their quantity. They will be references to
 * identify chunk boundaries inside each RAMBlock and also be referenced
 * during dynamic page registration.
 */
static void qemu_rdma_init_ram_blocks(RDMAContext *rdma)
{
    RDMALocalBlocks *local = &rdma->local_ram_blocks;
    int ret;

    assert(rdma->blockmap == NULL);
    memset(local, 0, sizeof *local);
    ret = foreach_not_ignored_block(qemu_rdma_init_one_block, rdma);
    assert(!ret);
    trace_qemu_rdma_init_ram_blocks(local->nb_blocks);
    rdma->dest_blocks = g_new0(RDMADestBlock,
                               rdma->local_ram_blocks.nb_blocks);
    local->init = true;
}

/*
 * Note: If used outside of cleanup, the caller must ensure that the destination
 * block structures are also updated
 */
static void rdma_delete_block(RDMAContext *rdma, RDMALocalBlock *block)
{
    RDMALocalBlocks *local = &rdma->local_ram_blocks;
    RDMALocalBlock *old = local->block;

    if (rdma->blockmap) {
        g_hash_table_remove(rdma->blockmap, (void *)(uintptr_t)block->offset);
    }
    if (block->pmr) {
        for (int j = 0; j < block->nb_chunks; j++) {
            if (!block->pmr[j]) {
                continue;
            }
            ibv_dereg_mr(block->pmr[j]);
            rdma->total_registrations--;
        }
        g_free(block->pmr);
        block->pmr = NULL;
    }

    if (block->mr) {
        ibv_dereg_mr(block->mr);
        rdma->total_registrations--;
        block->mr = NULL;
    }

    g_free(block->transit_bitmap);
    block->transit_bitmap = NULL;

    g_free(block->unregister_bitmap);
    block->unregister_bitmap = NULL;

    g_free(block->remote_keys);
    block->remote_keys = NULL;

    g_free(block->block_name);
    block->block_name = NULL;

    if (rdma->blockmap) {
        for (int x = 0; x < local->nb_blocks; x++) {
            g_hash_table_remove(rdma->blockmap,
                                (void *)(uintptr_t)old[x].offset);
        }
    }

    if (local->nb_blocks > 1) {

        local->block = g_new0(RDMALocalBlock, local->nb_blocks - 1);

        if (block->index) {
            memcpy(local->block, old, sizeof(RDMALocalBlock) * block->index);
        }

        if (block->index < (local->nb_blocks - 1)) {
            memcpy(local->block + block->index, old + (block->index + 1),
                sizeof(RDMALocalBlock) *
                    (local->nb_blocks - (block->index + 1)));
            for (int x = block->index; x < local->nb_blocks - 1; x++) {
                local->block[x].index--;
            }
        }
    } else {
        assert(block == local->block);
        local->block = NULL;
    }

    trace_rdma_delete_block(block, (uintptr_t)block->local_host_addr,
                           block->offset, block->length,
                            (uintptr_t)(block->local_host_addr + block->length),
                           BITS_TO_LONGS(block->nb_chunks) *
                               sizeof(unsigned long) * 8, block->nb_chunks);

    g_free(old);

    local->nb_blocks--;

    if (local->nb_blocks && rdma->blockmap) {
        for (int x = 0; x < local->nb_blocks; x++) {
            g_hash_table_insert(rdma->blockmap,
                                (void *)(uintptr_t)local->block[x].offset,
                                &local->block[x]);
        }
    }
}

/*
 * Trace RDMA device open, with device details.
 */
static void qemu_rdma_dump_id(const char *who, struct ibv_context *verbs)
{
    struct ibv_port_attr port;

    if (ibv_query_port(verbs, 1, &port)) {
        trace_qemu_rdma_dump_id_failed(who);
        return;
    }

    trace_qemu_rdma_dump_id(who,
                verbs->device->name,
                verbs->device->dev_name,
                verbs->device->dev_path,
                verbs->device->ibdev_path,
                port.link_layer,
                port.link_layer == IBV_LINK_LAYER_INFINIBAND ? "Infiniband"
                : port.link_layer == IBV_LINK_LAYER_ETHERNET ? "Ethernet"
                : "Unknown");
}

/*
 * Trace RDMA gid addressing information.
 * Useful for understanding the RDMA device hierarchy in the kernel.
 */
static void qemu_rdma_dump_gid(const char *who, struct rdma_cm_id *id)
{
    char sgid[33];
    char dgid[33];
    inet_ntop(AF_INET6, &id->route.addr.addr.ibaddr.sgid, sgid, sizeof sgid);
    inet_ntop(AF_INET6, &id->route.addr.addr.ibaddr.dgid, dgid, sizeof dgid);
    trace_qemu_rdma_dump_gid(who, sgid, dgid);
}

/*
 * As of now, IPv6 over RoCE / iWARP is not supported by linux.
 * We will try the next addrinfo struct, and fail if there are
 * no other valid addresses to bind against.
 *
 * If user is listening on '[::]', then we will not have a opened a device
 * yet and have no way of verifying if the device is RoCE or not.
 *
 * In this case, the source VM will throw an error for ALL types of
 * connections (both IPv4 and IPv6) if the destination machine does not have
 * a regular infiniband network available for use.
 *
 * The only way to guarantee that an error is thrown for broken kernels is
 * for the management software to choose a *specific* interface at bind time
 * and validate what time of hardware it is.
 *
 * Unfortunately, this puts the user in a fix:
 *
 *  If the source VM connects with an IPv4 address without knowing that the
 *  destination has bound to '[::]' the migration will unconditionally fail
 *  unless the management software is explicitly listening on the IPv4
 *  address while using a RoCE-based device.
 *
 *  If the source VM connects with an IPv6 address, then we're OK because we can
 *  throw an error on the source (and similarly on the destination).
 *
 *  But in mixed environments, this will be broken for a while until it is fixed
 *  inside linux.
 *
 * We do provide a *tiny* bit of help in this function: We can list all of the
 * devices in the system and check to see if all the devices are RoCE or
 * Infiniband.
 *
 * If we detect that we have a *pure* RoCE environment, then we can safely
 * thrown an error even if the management software has specified '[::]' as the
 * bind address.
 *
 * However, if there is are multiple hetergeneous devices, then we cannot make
 * this assumption and the user just has to be sure they know what they are
 * doing.
 *
 * Patches are being reviewed on linux-rdma.
 */
static int qemu_rdma_broken_ipv6_kernel(struct ibv_context *verbs, Error **errp)
{
    /* This bug only exists in linux, to our knowledge. */
#ifdef CONFIG_LINUX
    struct ibv_port_attr port_attr;

    /*
     * Verbs are only NULL if management has bound to '[::]'.
     *
     * Let's iterate through all the devices and see if there any pure IB
     * devices (non-ethernet).
     *
     * If not, then we can safely proceed with the migration.
     * Otherwise, there are no guarantees until the bug is fixed in linux.
     */
    if (!verbs) {
        int num_devices;
        struct ibv_device **dev_list = ibv_get_device_list(&num_devices);
        bool roce_found = false;
        bool ib_found = false;

        for (int x = 0; x < num_devices; x++) {
            verbs = ibv_open_device(dev_list[x]);
            /*
             * ibv_open_device() is not documented to set errno.  If
             * it does, it's somebody else's doc bug.  If it doesn't,
             * the use of errno below is wrong.
             * TODO Find out whether ibv_open_device() sets errno.
             */
            if (!verbs) {
                if (errno == EPERM) {
                    continue;
                } else {
                    error_setg_errno(errp, errno,
                                     "could not open RDMA device context");
                    return -1;
                }
            }

            if (ibv_query_port(verbs, 1, &port_attr)) {
                ibv_close_device(verbs);
                error_setg(errp,
                           "RDMA ERROR: Could not query initial IB port");
                return -1;
            }

            if (port_attr.link_layer == IBV_LINK_LAYER_INFINIBAND) {
                ib_found = true;
            } else if (port_attr.link_layer == IBV_LINK_LAYER_ETHERNET) {
                roce_found = true;
            }

            ibv_close_device(verbs);

        }

        if (roce_found) {
            if (ib_found) {
                warn_report("migrations may fail:"
                            " IPv6 over RoCE / iWARP in linux"
                            " is broken. But since you appear to have a"
                            " mixed RoCE / IB environment, be sure to only"
                            " migrate over the IB fabric until the kernel "
                            " fixes the bug.");
            } else {
                error_setg(errp, "RDMA ERROR: "
                           "You only have RoCE / iWARP devices in your systems"
                           " and your management software has specified '[::]'"
                           ", but IPv6 over RoCE / iWARP is not supported in Linux.");
                return -1;
            }
        }

        return 0;
    }

    /*
     * If we have a verbs context, that means that some other than '[::]' was
     * used by the management software for binding. In which case we can
     * actually warn the user about a potentially broken kernel.
     */

    /* IB ports start with 1, not 0 */
    if (ibv_query_port(verbs, 1, &port_attr)) {
        error_setg(errp, "RDMA ERROR: Could not query initial IB port");
        return -1;
    }

    if (port_attr.link_layer == IBV_LINK_LAYER_ETHERNET) {
        error_setg(errp, "RDMA ERROR: "
                   "Linux kernel's RoCE / iWARP does not support IPv6 "
                   "(but patches on linux-rdma in progress)");
        return -1;
    }

#endif

    return 0;
}

/*
 * Figure out which RDMA device corresponds to the requested IP hostname
 * Also create the initial connection manager identifiers for opening
 * the connection.
 */
static int qemu_rdma_resolve_host(RDMAContext *rdma, Error **errp)
{
    Error *err = NULL;
    int ret;
    struct rdma_addrinfo *res;
    char port_str[16];
    struct rdma_cm_event *cm_event;
    char ip[40] = "unknown";

    if (rdma->host == NULL || !strcmp(rdma->host, "")) {
        error_setg(errp, "RDMA ERROR: RDMA hostname has not been set");
        return -1;
    }

    /* create CM channel */
    rdma->channel = rdma_create_event_channel();
    if (!rdma->channel) {
        error_setg(errp, "RDMA ERROR: could not create CM channel");
        return -1;
    }

    /* create CM id */
    ret = rdma_create_id(rdma->channel, &rdma->cm_id, NULL, RDMA_PS_TCP);
    if (ret < 0) {
        error_setg(errp, "RDMA ERROR: could not create channel id");
        goto err_resolve_create_id;
    }

    snprintf(port_str, 16, "%d", rdma->port);
    port_str[15] = '\0';

    ret = rdma_getaddrinfo(rdma->host, port_str, NULL, &res);
    if (ret) {
        error_setg(errp, "RDMA ERROR: could not rdma_getaddrinfo address %s",
                   rdma->host);
        goto err_resolve_get_addr;
    }

    /* Try all addresses, saving the first error in @err */
    for (struct rdma_addrinfo *e = res; e != NULL; e = e->ai_next) {
        Error **local_errp = err ? NULL : &err;

        inet_ntop(e->ai_family,
            &((struct sockaddr_in *) e->ai_dst_addr)->sin_addr, ip, sizeof ip);
        trace_qemu_rdma_resolve_host_trying(rdma->host, ip);

        ret = rdma_resolve_addr(rdma->cm_id, NULL, e->ai_dst_addr,
                RDMA_RESOLVE_TIMEOUT_MS);
        if (ret >= 0) {
            if (e->ai_family == AF_INET6) {
                ret = qemu_rdma_broken_ipv6_kernel(rdma->cm_id->verbs,
                                                   local_errp);
                if (ret < 0) {
                    continue;
                }
            }
            error_free(err);
            goto route;
        }
    }

    rdma_freeaddrinfo(res);
    if (err) {
        error_propagate(errp, err);
    } else {
        error_setg(errp, "RDMA ERROR: could not resolve address %s",
                   rdma->host);
    }
    goto err_resolve_get_addr;

route:
    rdma_freeaddrinfo(res);
    qemu_rdma_dump_gid("source_resolve_addr", rdma->cm_id);

    ret = rdma_get_cm_event(rdma->channel, &cm_event);
    if (ret < 0) {
        error_setg(errp, "RDMA ERROR: could not perform event_addr_resolved");
        goto err_resolve_get_addr;
    }

    if (cm_event->event != RDMA_CM_EVENT_ADDR_RESOLVED) {
        error_setg(errp,
                   "RDMA ERROR: result not equal to event_addr_resolved %s",
                   rdma_event_str(cm_event->event));
        rdma_ack_cm_event(cm_event);
        goto err_resolve_get_addr;
    }
    rdma_ack_cm_event(cm_event);

    /* resolve route */
    ret = rdma_resolve_route(rdma->cm_id, RDMA_RESOLVE_TIMEOUT_MS);
    if (ret < 0) {
        error_setg(errp, "RDMA ERROR: could not resolve rdma route");
        goto err_resolve_get_addr;
    }

    ret = rdma_get_cm_event(rdma->channel, &cm_event);
    if (ret < 0) {
        error_setg(errp, "RDMA ERROR: could not perform event_route_resolved");
        goto err_resolve_get_addr;
    }
    if (cm_event->event != RDMA_CM_EVENT_ROUTE_RESOLVED) {
        error_setg(errp, "RDMA ERROR: "
                   "result not equal to event_route_resolved: %s",
                   rdma_event_str(cm_event->event));
        rdma_ack_cm_event(cm_event);
        goto err_resolve_get_addr;
    }
    rdma_ack_cm_event(cm_event);
    rdma->verbs = rdma->cm_id->verbs;
    qemu_rdma_dump_id("source_resolve_host", rdma->cm_id->verbs);
    qemu_rdma_dump_gid("source_resolve_host", rdma->cm_id);
    return 0;

err_resolve_get_addr:
    rdma_destroy_id(rdma->cm_id);
    rdma->cm_id = NULL;
err_resolve_create_id:
    rdma_destroy_event_channel(rdma->channel);
    rdma->channel = NULL;
    return -1;
}

/*
 * Create protection domain and completion queues
 */
static int qemu_rdma_alloc_pd_cq(RDMAContext *rdma, Error **errp)
{
    /* allocate pd */
    rdma->pd = ibv_alloc_pd(rdma->verbs);
    if (!rdma->pd) {
        error_setg(errp, "failed to allocate protection domain");
        return -1;
    }

    /* create receive completion channel */
    rdma->recv_comp_channel = ibv_create_comp_channel(rdma->verbs);
    if (!rdma->recv_comp_channel) {
        error_setg(errp, "failed to allocate receive completion channel");
        goto err_alloc_pd_cq;
    }

    /*
     * Completion queue can be filled by read work requests.
     */
    rdma->recv_cq = ibv_create_cq(rdma->verbs, (RDMA_SIGNALED_SEND_MAX * 3),
                                  NULL, rdma->recv_comp_channel, 0);
    if (!rdma->recv_cq) {
        error_setg(errp, "failed to allocate receive completion queue");
        goto err_alloc_pd_cq;
    }

    /* create send completion channel */
    rdma->send_comp_channel = ibv_create_comp_channel(rdma->verbs);
    if (!rdma->send_comp_channel) {
        error_setg(errp, "failed to allocate send completion channel");
        goto err_alloc_pd_cq;
    }

    rdma->send_cq = ibv_create_cq(rdma->verbs, (RDMA_SIGNALED_SEND_MAX * 3),
                                  NULL, rdma->send_comp_channel, 0);
    if (!rdma->send_cq) {
        error_setg(errp, "failed to allocate send completion queue");
        goto err_alloc_pd_cq;
    }

    return 0;

err_alloc_pd_cq:
    if (rdma->pd) {
        ibv_dealloc_pd(rdma->pd);
    }
    if (rdma->recv_comp_channel) {
        ibv_destroy_comp_channel(rdma->recv_comp_channel);
    }
    if (rdma->send_comp_channel) {
        ibv_destroy_comp_channel(rdma->send_comp_channel);
    }
    if (rdma->recv_cq) {
        ibv_destroy_cq(rdma->recv_cq);
        rdma->recv_cq = NULL;
    }
    rdma->pd = NULL;
    rdma->recv_comp_channel = NULL;
    rdma->send_comp_channel = NULL;
    return -1;

}

/*
 * Create queue pairs.
 */
static int qemu_rdma_alloc_qp(RDMAContext *rdma)
{
    struct ibv_qp_init_attr attr = { 0 };

    attr.cap.max_send_wr = RDMA_SIGNALED_SEND_MAX;
    attr.cap.max_recv_wr = 3;
    attr.cap.max_send_sge = 1;
    attr.cap.max_recv_sge = 1;
    attr.send_cq = rdma->send_cq;
    attr.recv_cq = rdma->recv_cq;
    attr.qp_type = IBV_QPT_RC;

    if (rdma_create_qp(rdma->cm_id, rdma->pd, &attr) < 0) {
        return -1;
    }

    rdma->qp = rdma->cm_id->qp;
    return 0;
}

/* Check whether On-Demand Paging is supported by RDAM device */
static bool rdma_support_odp(struct ibv_context *dev)
{
    struct ibv_device_attr_ex attr = {0};

    if (ibv_query_device_ex(dev, NULL, &attr)) {
        return false;
    }

    if (attr.odp_caps.general_caps & IBV_ODP_SUPPORT) {
        return true;
    }

    return false;
}

/*
 * ibv_advise_mr to avoid RNR NAK error as far as possible.
 * The responder mr registering with ODP will sent RNR NAK back to
 * the requester in the face of the page fault.
 */
static void qemu_rdma_advise_prefetch_mr(struct ibv_pd *pd, uint64_t addr,
                                         uint32_t len,  uint32_t lkey,
                                         const char *name, bool wr)
{
#ifdef HAVE_IBV_ADVISE_MR
    int ret;
    int advice = wr ? IBV_ADVISE_MR_ADVICE_PREFETCH_WRITE :
                 IBV_ADVISE_MR_ADVICE_PREFETCH;
    struct ibv_sge sg_list = {.lkey = lkey, .addr = addr, .length = len};

    ret = ibv_advise_mr(pd, advice,
                        IBV_ADVISE_MR_FLAG_FLUSH, &sg_list, 1);
    /* ignore the error */
    trace_qemu_rdma_advise_mr(name, len, addr, strerror(ret));
#endif
}

static int qemu_rdma_reg_whole_ram_blocks(RDMAContext *rdma, Error **errp)
{
    int i;
    RDMALocalBlocks *local = &rdma->local_ram_blocks;

    for (i = 0; i < local->nb_blocks; i++) {
        int access = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE;

        local->block[i].mr =
            ibv_reg_mr(rdma->pd,
                    local->block[i].local_host_addr,
                    local->block[i].length, access
                    );
        /*
         * ibv_reg_mr() is not documented to set errno.  If it does,
         * it's somebody else's doc bug.  If it doesn't, the use of
         * errno below is wrong.
         * TODO Find out whether ibv_reg_mr() sets errno.
         */
        if (!local->block[i].mr &&
            errno == ENOTSUP && rdma_support_odp(rdma->verbs)) {
                access |= IBV_ACCESS_ON_DEMAND;
                /* register ODP mr */
                local->block[i].mr =
                    ibv_reg_mr(rdma->pd,
                               local->block[i].local_host_addr,
                               local->block[i].length, access);
                trace_qemu_rdma_register_odp_mr(local->block[i].block_name);

                if (local->block[i].mr) {
                    qemu_rdma_advise_prefetch_mr(rdma->pd,
                                    (uintptr_t)local->block[i].local_host_addr,
                                    local->block[i].length,
                                    local->block[i].mr->lkey,
                                    local->block[i].block_name,
                                    true);
                }
        }

        if (!local->block[i].mr) {
            error_setg_errno(errp, errno,
                             "Failed to register local dest ram block!");
            goto err;
        }
        rdma->total_registrations++;
    }

    return 0;

err:
    for (i--; i >= 0; i--) {
        ibv_dereg_mr(local->block[i].mr);
        local->block[i].mr = NULL;
        rdma->total_registrations--;
    }

    return -1;

}

/*
 * Find the ram block that corresponds to the page requested to be
 * transmitted by QEMU.
 *
 * Once the block is found, also identify which 'chunk' within that
 * block that the page belongs to.
 */
static void qemu_rdma_search_ram_block(RDMAContext *rdma,
                                       uintptr_t block_offset,
                                       uint64_t offset,
                                       uint64_t length,
                                       uint64_t *block_index,
                                       uint64_t *chunk_index)
{
    uint64_t current_addr = block_offset + offset;
    RDMALocalBlock *block = g_hash_table_lookup(rdma->blockmap,
                                                (void *) block_offset);
    assert(block);
    assert(current_addr >= block->offset);
    assert((current_addr + length) <= (block->offset + block->length));

    *block_index = block->index;
    *chunk_index = ram_chunk_index(block->local_host_addr,
                block->local_host_addr + (current_addr - block->offset));
}

/*
 * Register a chunk with IB. If the chunk was already registered
 * previously, then skip.
 *
 * Also return the keys associated with the registration needed
 * to perform the actual RDMA operation.
 */
static int qemu_rdma_register_and_get_keys(RDMAContext *rdma,
        RDMALocalBlock *block, uintptr_t host_addr,
        uint32_t *lkey, uint32_t *rkey, int chunk,
        uint8_t *chunk_start, uint8_t *chunk_end)
{
    if (block->mr) {
        if (lkey) {
            *lkey = block->mr->lkey;
        }
        if (rkey) {
            *rkey = block->mr->rkey;
        }
        return 0;
    }

    /* allocate memory to store chunk MRs */
    if (!block->pmr) {
        block->pmr = g_new0(struct ibv_mr *, block->nb_chunks);
    }

    /*
     * If 'rkey', then we're the destination, so grant access to the source.
     *
     * If 'lkey', then we're the source VM, so grant access only to ourselves.
     */
    if (!block->pmr[chunk]) {
        uint64_t len = chunk_end - chunk_start;
        int access = rkey ? IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE :
                     0;

        trace_qemu_rdma_register_and_get_keys(len, chunk_start);

        block->pmr[chunk] = ibv_reg_mr(rdma->pd, chunk_start, len, access);
        /*
         * ibv_reg_mr() is not documented to set errno.  If it does,
         * it's somebody else's doc bug.  If it doesn't, the use of
         * errno below is wrong.
         * TODO Find out whether ibv_reg_mr() sets errno.
         */
        if (!block->pmr[chunk] &&
            errno == ENOTSUP && rdma_support_odp(rdma->verbs)) {
            access |= IBV_ACCESS_ON_DEMAND;
            /* register ODP mr */
            block->pmr[chunk] = ibv_reg_mr(rdma->pd, chunk_start, len, access);
            trace_qemu_rdma_register_odp_mr(block->block_name);

            if (block->pmr[chunk]) {
                qemu_rdma_advise_prefetch_mr(rdma->pd, (uintptr_t)chunk_start,
                                            len, block->pmr[chunk]->lkey,
                                            block->block_name, rkey);

            }
        }
    }
    if (!block->pmr[chunk]) {
        return -1;
    }
    rdma->total_registrations++;

    if (lkey) {
        *lkey = block->pmr[chunk]->lkey;
    }
    if (rkey) {
        *rkey = block->pmr[chunk]->rkey;
    }
    return 0;
}

/*
 * Register (at connection time) the memory used for control
 * channel messages.
 */
static int qemu_rdma_reg_control(RDMAContext *rdma, int idx)
{
    rdma->wr_data[idx].control_mr = ibv_reg_mr(rdma->pd,
            rdma->wr_data[idx].control, RDMA_CONTROL_MAX_BUFFER,
            IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);
    if (rdma->wr_data[idx].control_mr) {
        rdma->total_registrations++;
        return 0;
    }
    return -1;
}

/*
 * Perform a non-optimized memory unregistration after every transfer
 * for demonstration purposes, only if pin-all is not requested.
 *
 * Potential optimizations:
 * 1. Start a new thread to run this function continuously
        - for bit clearing
        - and for receipt of unregister messages
 * 2. Use an LRU.
 * 3. Use workload hints.
 */
static int qemu_rdma_unregister_waiting(RDMAContext *rdma)
{
    Error *err = NULL;

    while (rdma->unregistrations[rdma->unregister_current]) {
        int ret;
        uint64_t wr_id = rdma->unregistrations[rdma->unregister_current];
        uint64_t chunk =
            (wr_id & RDMA_WRID_CHUNK_MASK) >> RDMA_WRID_CHUNK_SHIFT;
        uint64_t index =
            (wr_id & RDMA_WRID_BLOCK_MASK) >> RDMA_WRID_BLOCK_SHIFT;
        RDMALocalBlock *block =
            &(rdma->local_ram_blocks.block[index]);
        RDMARegister reg = { .current_index = index };
        RDMAControlHeader resp = { .type = RDMA_CONTROL_UNREGISTER_FINISHED,
                                 };
        RDMAControlHeader head = { .len = sizeof(RDMARegister),
                                   .type = RDMA_CONTROL_UNREGISTER_REQUEST,
                                   .repeat = 1,
                                 };

        trace_qemu_rdma_unregister_waiting_proc(chunk,
                                                rdma->unregister_current);

        rdma->unregistrations[rdma->unregister_current] = 0;
        rdma->unregister_current++;

        if (rdma->unregister_current == RDMA_SIGNALED_SEND_MAX) {
            rdma->unregister_current = 0;
        }


        /*
         * Unregistration is speculative (because migration is single-threaded
         * and we cannot break the protocol's inifinband message ordering).
         * Thus, if the memory is currently being used for transmission,
         * then abort the attempt to unregister and try again
         * later the next time a completion is received for this memory.
         */
        clear_bit(chunk, block->unregister_bitmap);

        if (test_bit(chunk, block->transit_bitmap)) {
            trace_qemu_rdma_unregister_waiting_inflight(chunk);
            continue;
        }

        trace_qemu_rdma_unregister_waiting_send(chunk);

        ret = ibv_dereg_mr(block->pmr[chunk]);
        block->pmr[chunk] = NULL;
        block->remote_keys[chunk] = 0;

        if (ret != 0) {
            error_report("unregistration chunk failed: %s",
                         strerror(ret));
            return -1;
        }
        rdma->total_registrations--;

        reg.key.chunk = chunk;
        register_to_network(rdma, &reg);
        ret = qemu_rdma_exchange_send(rdma, &head, (uint8_t *) &reg,
                                      &resp, NULL, NULL, &err);
        if (ret < 0) {
            error_report_err(err);
            return -1;
        }

        trace_qemu_rdma_unregister_waiting_complete(chunk);
    }

    return 0;
}

static uint64_t qemu_rdma_make_wrid(uint64_t wr_id, uint64_t index,
                                         uint64_t chunk)
{
    uint64_t result = wr_id & RDMA_WRID_TYPE_MASK;

    result |= (index << RDMA_WRID_BLOCK_SHIFT);
    result |= (chunk << RDMA_WRID_CHUNK_SHIFT);

    return result;
}

/*
 * Consult the connection manager to see a work request
 * (of any kind) has completed.
 * Return the work request ID that completed.
 */
static int qemu_rdma_poll(RDMAContext *rdma, struct ibv_cq *cq,
                          uint64_t *wr_id_out, uint32_t *byte_len)
{
    int ret;
    struct ibv_wc wc;
    uint64_t wr_id;

    ret = ibv_poll_cq(cq, 1, &wc);

    if (!ret) {
        *wr_id_out = RDMA_WRID_NONE;
        return 0;
    }

    if (ret < 0) {
        return -1;
    }

    wr_id = wc.wr_id & RDMA_WRID_TYPE_MASK;

    if (wc.status != IBV_WC_SUCCESS) {
        return -1;
    }

    if (rdma->control_ready_expected &&
        (wr_id >= RDMA_WRID_RECV_CONTROL)) {
        trace_qemu_rdma_poll_recv(wr_id - RDMA_WRID_RECV_CONTROL, wr_id,
                                  rdma->nb_sent);
        rdma->control_ready_expected = 0;
    }

    if (wr_id == RDMA_WRID_RDMA_WRITE) {
        uint64_t chunk =
            (wc.wr_id & RDMA_WRID_CHUNK_MASK) >> RDMA_WRID_CHUNK_SHIFT;
        uint64_t index =
            (wc.wr_id & RDMA_WRID_BLOCK_MASK) >> RDMA_WRID_BLOCK_SHIFT;
        RDMALocalBlock *block = &(rdma->local_ram_blocks.block[index]);

        trace_qemu_rdma_poll_write(wr_id, rdma->nb_sent,
                                   index, chunk, block->local_host_addr,
                                   (void *)(uintptr_t)block->remote_host_addr);

        clear_bit(chunk, block->transit_bitmap);

        if (rdma->nb_sent > 0) {
            rdma->nb_sent--;
        }
    } else {
        trace_qemu_rdma_poll_other(wr_id, rdma->nb_sent);
    }

    *wr_id_out = wc.wr_id;
    if (byte_len) {
        *byte_len = wc.byte_len;
    }

    return  0;
}

/* Wait for activity on the completion channel.
 * Returns 0 on success, none-0 on error.
 */
static int qemu_rdma_wait_comp_channel(RDMAContext *rdma,
                                       struct ibv_comp_channel *comp_channel)
{
    struct rdma_cm_event *cm_event;

    /*
     * Coroutine doesn't start until migration_fd_process_incoming()
     * so don't yield unless we know we're running inside of a coroutine.
     */
    if (rdma->migration_started_on_destination &&
        migration_incoming_get_current()->state == MIGRATION_STATUS_ACTIVE) {
        yield_until_fd_readable(comp_channel->fd);
    } else {
        /* This is the source side, we're in a separate thread
         * or destination prior to migration_fd_process_incoming()
         * after postcopy, the destination also in a separate thread.
         * we can't yield; so we have to poll the fd.
         * But we need to be able to handle 'cancel' or an error
         * without hanging forever.
         */
        while (!rdma->errored && !rdma->received_error) {
            GPollFD pfds[2];
            pfds[0].fd = comp_channel->fd;
            pfds[0].events = G_IO_IN | G_IO_HUP | G_IO_ERR;
            pfds[0].revents = 0;

            pfds[1].fd = rdma->channel->fd;
            pfds[1].events = G_IO_IN | G_IO_HUP | G_IO_ERR;
            pfds[1].revents = 0;

            /* 0.1s timeout, should be fine for a 'cancel' */
            switch (qemu_poll_ns(pfds, 2, 100 * 1000 * 1000)) {
            case 2:
            case 1: /* fd active */
                if (pfds[0].revents) {
                    return 0;
                }

                if (pfds[1].revents) {
                    if (rdma_get_cm_event(rdma->channel, &cm_event) < 0) {
                        return -1;
                    }

                    if (cm_event->event == RDMA_CM_EVENT_DISCONNECTED ||
                        cm_event->event == RDMA_CM_EVENT_DEVICE_REMOVAL) {
                        rdma_ack_cm_event(cm_event);
                        return -1;
                    }
                    rdma_ack_cm_event(cm_event);
                }
                break;

            case 0: /* Timeout, go around again */
                break;

            default: /* Error of some type -
                      * I don't trust errno from qemu_poll_ns
                     */
                return -1;
            }

            if (migrate_get_current()->state == MIGRATION_STATUS_CANCELLING) {
                /* Bail out and let the cancellation happen */
                return -1;
            }
        }
    }

    if (rdma->received_error) {
        return -1;
    }
    return -rdma->errored;
}

static struct ibv_comp_channel *to_channel(RDMAContext *rdma, uint64_t wrid)
{
    return wrid < RDMA_WRID_RECV_CONTROL ? rdma->send_comp_channel :
           rdma->recv_comp_channel;
}

static struct ibv_cq *to_cq(RDMAContext *rdma, uint64_t wrid)
{
    return wrid < RDMA_WRID_RECV_CONTROL ? rdma->send_cq : rdma->recv_cq;
}

/*
 * Block until the next work request has completed.
 *
 * First poll to see if a work request has already completed,
 * otherwise block.
 *
 * If we encounter completed work requests for IDs other than
 * the one we're interested in, then that's generally an error.
 *
 * The only exception is actual RDMA Write completions. These
 * completions only need to be recorded, but do not actually
 * need further processing.
 */
static int qemu_rdma_block_for_wrid(RDMAContext *rdma,
                                    uint64_t wrid_requested,
                                    uint32_t *byte_len)
{
    int num_cq_events = 0, ret;
    struct ibv_cq *cq;
    void *cq_ctx;
    uint64_t wr_id = RDMA_WRID_NONE, wr_id_in;
    struct ibv_comp_channel *ch = to_channel(rdma, wrid_requested);
    struct ibv_cq *poll_cq = to_cq(rdma, wrid_requested);

    if (ibv_req_notify_cq(poll_cq, 0)) {
        return -1;
    }
    /* poll cq first */
    while (wr_id != wrid_requested) {
        ret = qemu_rdma_poll(rdma, poll_cq, &wr_id_in, byte_len);
        if (ret < 0) {
            return -1;
        }

        wr_id = wr_id_in & RDMA_WRID_TYPE_MASK;

        if (wr_id == RDMA_WRID_NONE) {
            break;
        }
        if (wr_id != wrid_requested) {
            trace_qemu_rdma_block_for_wrid_miss(wrid_requested, wr_id);
        }
    }

    if (wr_id == wrid_requested) {
        return 0;
    }

    while (1) {
        ret = qemu_rdma_wait_comp_channel(rdma, ch);
        if (ret < 0) {
            goto err_block_for_wrid;
        }

        ret = ibv_get_cq_event(ch, &cq, &cq_ctx);
        if (ret < 0) {
            goto err_block_for_wrid;
        }

        num_cq_events++;

        if (ibv_req_notify_cq(cq, 0)) {
            goto err_block_for_wrid;
        }

        while (wr_id != wrid_requested) {
            ret = qemu_rdma_poll(rdma, poll_cq, &wr_id_in, byte_len);
            if (ret < 0) {
                goto err_block_for_wrid;
            }

            wr_id = wr_id_in & RDMA_WRID_TYPE_MASK;

            if (wr_id == RDMA_WRID_NONE) {
                break;
            }
            if (wr_id != wrid_requested) {
                trace_qemu_rdma_block_for_wrid_miss(wrid_requested, wr_id);
            }
        }

        if (wr_id == wrid_requested) {
            goto success_block_for_wrid;
        }
    }

success_block_for_wrid:
    if (num_cq_events) {
        ibv_ack_cq_events(cq, num_cq_events);
    }
    return 0;

err_block_for_wrid:
    if (num_cq_events) {
        ibv_ack_cq_events(cq, num_cq_events);
    }

    rdma->errored = true;
    return -1;
}

/*
 * Post a SEND message work request for the control channel
 * containing some data and block until the post completes.
 */
static int qemu_rdma_post_send_control(RDMAContext *rdma, uint8_t *buf,
                                       RDMAControlHeader *head,
                                       Error **errp)
{
    int ret;
    RDMAWorkRequestData *wr = &rdma->wr_data[RDMA_WRID_CONTROL];
    struct ibv_send_wr *bad_wr;
    struct ibv_sge sge = {
                           .addr = (uintptr_t)(wr->control),
                           .length = head->len + sizeof(RDMAControlHeader),
                           .lkey = wr->control_mr->lkey,
                         };
    struct ibv_send_wr send_wr = {
                                   .wr_id = RDMA_WRID_SEND_CONTROL,
                                   .opcode = IBV_WR_SEND,
                                   .send_flags = IBV_SEND_SIGNALED,
                                   .sg_list = &sge,
                                   .num_sge = 1,
                                };

    trace_qemu_rdma_post_send_control(control_desc(head->type));

    /*
     * We don't actually need to do a memcpy() in here if we used
     * the "sge" properly, but since we're only sending control messages
     * (not RAM in a performance-critical path), then its OK for now.
     *
     * The copy makes the RDMAControlHeader simpler to manipulate
     * for the time being.
     */
    assert(head->len <= RDMA_CONTROL_MAX_BUFFER - sizeof(*head));
    memcpy(wr->control, head, sizeof(RDMAControlHeader));
    control_to_network((void *) wr->control);

    if (buf) {
        memcpy(wr->control + sizeof(RDMAControlHeader), buf, head->len);
    }


    ret = ibv_post_send(rdma->qp, &send_wr, &bad_wr);

    if (ret > 0) {
        error_setg(errp, "Failed to use post IB SEND for control");
        return -1;
    }

    ret = qemu_rdma_block_for_wrid(rdma, RDMA_WRID_SEND_CONTROL, NULL);
    if (ret < 0) {
        error_setg(errp, "rdma migration: send polling control error");
        return -1;
    }

    return 0;
}

/*
 * Post a RECV work request in anticipation of some future receipt
 * of data on the control channel.
 */
static int qemu_rdma_post_recv_control(RDMAContext *rdma, int idx,
                                       Error **errp)
{
    struct ibv_recv_wr *bad_wr;
    struct ibv_sge sge = {
                            .addr = (uintptr_t)(rdma->wr_data[idx].control),
                            .length = RDMA_CONTROL_MAX_BUFFER,
                            .lkey = rdma->wr_data[idx].control_mr->lkey,
                         };

    struct ibv_recv_wr recv_wr = {
                                    .wr_id = RDMA_WRID_RECV_CONTROL + idx,
                                    .sg_list = &sge,
                                    .num_sge = 1,
                                 };


    if (ibv_post_recv(rdma->qp, &recv_wr, &bad_wr)) {
        error_setg(errp, "error posting control recv");
        return -1;
    }

    return 0;
}

/*
 * Block and wait for a RECV control channel message to arrive.
 */
static int qemu_rdma_exchange_get_response(RDMAContext *rdma,
                RDMAControlHeader *head, uint32_t expecting, int idx,
                Error **errp)
{
    uint32_t byte_len;
    int ret = qemu_rdma_block_for_wrid(rdma, RDMA_WRID_RECV_CONTROL + idx,
                                       &byte_len);

    if (ret < 0) {
        error_setg(errp, "rdma migration: recv polling control error!");
        return -1;
    }

    network_to_control((void *) rdma->wr_data[idx].control);
    memcpy(head, rdma->wr_data[idx].control, sizeof(RDMAControlHeader));

    trace_qemu_rdma_exchange_get_response_start(control_desc(expecting));

    if (expecting == RDMA_CONTROL_NONE) {
        trace_qemu_rdma_exchange_get_response_none(control_desc(head->type),
                                             head->type);
    } else if (head->type != expecting || head->type == RDMA_CONTROL_ERROR) {
        error_setg(errp, "Was expecting a %s (%d) control message"
                ", but got: %s (%d), length: %d",
                control_desc(expecting), expecting,
                control_desc(head->type), head->type, head->len);
        if (head->type == RDMA_CONTROL_ERROR) {
            rdma->received_error = true;
        }
        return -1;
    }
    if (head->len > RDMA_CONTROL_MAX_BUFFER - sizeof(*head)) {
        error_setg(errp, "too long length: %d", head->len);
        return -1;
    }
    if (sizeof(*head) + head->len != byte_len) {
        error_setg(errp, "Malformed length: %d byte_len %d",
                   head->len, byte_len);
        return -1;
    }

    return 0;
}

/*
 * When a RECV work request has completed, the work request's
 * buffer is pointed at the header.
 *
 * This will advance the pointer to the data portion
 * of the control message of the work request's buffer that
 * was populated after the work request finished.
 */
static void qemu_rdma_move_header(RDMAContext *rdma, int idx,
                                  RDMAControlHeader *head)
{
    rdma->wr_data[idx].control_len = head->len;
    rdma->wr_data[idx].control_curr =
        rdma->wr_data[idx].control + sizeof(RDMAControlHeader);
}

/*
 * This is an 'atomic' high-level operation to deliver a single, unified
 * control-channel message.
 *
 * Additionally, if the user is expecting some kind of reply to this message,
 * they can request a 'resp' response message be filled in by posting an
 * additional work request on behalf of the user and waiting for an additional
 * completion.
 *
 * The extra (optional) response is used during registration to us from having
 * to perform an *additional* exchange of message just to provide a response by
 * instead piggy-backing on the acknowledgement.
 */
static int qemu_rdma_exchange_send(RDMAContext *rdma, RDMAControlHeader *head,
                                   uint8_t *data, RDMAControlHeader *resp,
                                   int *resp_idx,
                                   int (*callback)(RDMAContext *rdma,
                                                   Error **errp),
                                   Error **errp)
{
    int ret;

    /*
     * Wait until the dest is ready before attempting to deliver the message
     * by waiting for a READY message.
     */
    if (rdma->control_ready_expected) {
        RDMAControlHeader resp_ignored;

        ret = qemu_rdma_exchange_get_response(rdma, &resp_ignored,
                                              RDMA_CONTROL_READY,
                                              RDMA_WRID_READY, errp);
        if (ret < 0) {
            return -1;
        }
    }

    /*
     * If the user is expecting a response, post a WR in anticipation of it.
     */
    if (resp) {
        ret = qemu_rdma_post_recv_control(rdma, RDMA_WRID_DATA, errp);
        if (ret < 0) {
            return -1;
        }
    }

    /*
     * Post a WR to replace the one we just consumed for the READY message.
     */
    ret = qemu_rdma_post_recv_control(rdma, RDMA_WRID_READY, errp);
    if (ret < 0) {
        return -1;
    }

    /*
     * Deliver the control message that was requested.
     */
    ret = qemu_rdma_post_send_control(rdma, data, head, errp);

    if (ret < 0) {
        return -1;
    }

    /*
     * If we're expecting a response, block and wait for it.
     */
    if (resp) {
        if (callback) {
            trace_qemu_rdma_exchange_send_issue_callback();
            ret = callback(rdma, errp);
            if (ret < 0) {
                return -1;
            }
        }

        trace_qemu_rdma_exchange_send_waiting(control_desc(resp->type));
        ret = qemu_rdma_exchange_get_response(rdma, resp,
                                              resp->type, RDMA_WRID_DATA,
                                              errp);

        if (ret < 0) {
            return -1;
        }

        qemu_rdma_move_header(rdma, RDMA_WRID_DATA, resp);
        if (resp_idx) {
            *resp_idx = RDMA_WRID_DATA;
        }
        trace_qemu_rdma_exchange_send_received(control_desc(resp->type));
    }

    rdma->control_ready_expected = 1;

    return 0;
}

/*
 * This is an 'atomic' high-level operation to receive a single, unified
 * control-channel message.
 */
static int qemu_rdma_exchange_recv(RDMAContext *rdma, RDMAControlHeader *head,
                                   uint32_t expecting, Error **errp)
{
    RDMAControlHeader ready = {
                                .len = 0,
                                .type = RDMA_CONTROL_READY,
                                .repeat = 1,
                              };
    int ret;

    /*
     * Inform the source that we're ready to receive a message.
     */
    ret = qemu_rdma_post_send_control(rdma, NULL, &ready, errp);

    if (ret < 0) {
        return -1;
    }

    /*
     * Block and wait for the message.
     */
    ret = qemu_rdma_exchange_get_response(rdma, head,
                                          expecting, RDMA_WRID_READY, errp);

    if (ret < 0) {
        return -1;
    }

    qemu_rdma_move_header(rdma, RDMA_WRID_READY, head);

    /*
     * Post a new RECV work request to replace the one we just consumed.
     */
    ret = qemu_rdma_post_recv_control(rdma, RDMA_WRID_READY, errp);
    if (ret < 0) {
        return -1;
    }

    return 0;
}

/*
 * Write an actual chunk of memory using RDMA.
 *
 * If we're using dynamic registration on the dest-side, we have to
 * send a registration command first.
 */
static int qemu_rdma_write_one(RDMAContext *rdma,
                               int current_index, uint64_t current_addr,
                               uint64_t length, Error **errp)
{
    struct ibv_sge sge;
    struct ibv_send_wr send_wr = { 0 };
    struct ibv_send_wr *bad_wr;
    int reg_result_idx, ret, count = 0;
    uint64_t chunk, chunks;
    uint8_t *chunk_start, *chunk_end;
    RDMALocalBlock *block = &(rdma->local_ram_blocks.block[current_index]);
    RDMARegister reg;
    RDMARegisterResult *reg_result;
    RDMAControlHeader resp = { .type = RDMA_CONTROL_REGISTER_RESULT };
    RDMAControlHeader head = { .len = sizeof(RDMARegister),
                               .type = RDMA_CONTROL_REGISTER_REQUEST,
                               .repeat = 1,
                             };

retry:
    sge.addr = (uintptr_t)(block->local_host_addr +
                            (current_addr - block->offset));
    sge.length = length;

    chunk = ram_chunk_index(block->local_host_addr,
                            (uint8_t *)(uintptr_t)sge.addr);
    chunk_start = ram_chunk_start(block, chunk);

    if (block->is_ram_block) {
        chunks = length / (1UL << RDMA_REG_CHUNK_SHIFT);

        if (chunks && ((length % (1UL << RDMA_REG_CHUNK_SHIFT)) == 0)) {
            chunks--;
        }
    } else {
        chunks = block->length / (1UL << RDMA_REG_CHUNK_SHIFT);

        if (chunks && ((block->length % (1UL << RDMA_REG_CHUNK_SHIFT)) == 0)) {
            chunks--;
        }
    }

    trace_qemu_rdma_write_one_top(chunks + 1,
                                  (chunks + 1) *
                                  (1UL << RDMA_REG_CHUNK_SHIFT) / 1024 / 1024);

    chunk_end = ram_chunk_end(block, chunk + chunks);


    while (test_bit(chunk, block->transit_bitmap)) {
        (void)count;
        trace_qemu_rdma_write_one_block(count++, current_index, chunk,
                sge.addr, length, rdma->nb_sent, block->nb_chunks);

        ret = qemu_rdma_block_for_wrid(rdma, RDMA_WRID_RDMA_WRITE, NULL);

        if (ret < 0) {
            error_setg(errp, "Failed to Wait for previous write to complete "
                    "block %d chunk %" PRIu64
                    " current %" PRIu64 " len %" PRIu64 " %d",
                    current_index, chunk, sge.addr, length, rdma->nb_sent);
            return -1;
        }
    }

    if (!rdma->pin_all || !block->is_ram_block) {
        if (!block->remote_keys[chunk]) {
            /*
             * This chunk has not yet been registered, so first check to see
             * if the entire chunk is zero. If so, tell the other size to
             * memset() + madvise() the entire chunk without RDMA.
             */

            if (buffer_is_zero((void *)(uintptr_t)sge.addr, length)) {
                RDMACompress comp = {
                                        .offset = current_addr,
                                        .value = 0,
                                        .block_idx = current_index,
                                        .length = length,
                                    };

                head.len = sizeof(comp);
                head.type = RDMA_CONTROL_COMPRESS;

                trace_qemu_rdma_write_one_zero(chunk, sge.length,
                                               current_index, current_addr);

                compress_to_network(rdma, &comp);
                ret = qemu_rdma_exchange_send(rdma, &head,
                                (uint8_t *) &comp, NULL, NULL, NULL, errp);

                if (ret < 0) {
                    return -1;
                }

                /*
                 * TODO: Here we are sending something, but we are not
                 * accounting for anything transferred.  The following is wrong:
                 *
                 * stat64_add(&mig_stats.rdma_bytes, sge.length);
                 *
                 * because we are using some kind of compression.  I
                 * would think that head.len would be the more similar
                 * thing to a correct value.
                 */
                stat64_add(&mig_stats.zero_pages,
                           sge.length / qemu_target_page_size());
                return 1;
            }

            /*
             * Otherwise, tell other side to register.
             */
            reg.current_index = current_index;
            if (block->is_ram_block) {
                reg.key.current_addr = current_addr;
            } else {
                reg.key.chunk = chunk;
            }
            reg.chunks = chunks;

            trace_qemu_rdma_write_one_sendreg(chunk, sge.length, current_index,
                                              current_addr);

            register_to_network(rdma, &reg);
            ret = qemu_rdma_exchange_send(rdma, &head, (uint8_t *) &reg,
                                    &resp, &reg_result_idx, NULL, errp);
            if (ret < 0) {
                return -1;
            }

            /* try to overlap this single registration with the one we sent. */
            if (qemu_rdma_register_and_get_keys(rdma, block, sge.addr,
                                                &sge.lkey, NULL, chunk,
                                                chunk_start, chunk_end)) {
                error_setg(errp, "cannot get lkey");
                return -1;
            }

            reg_result = (RDMARegisterResult *)
                    rdma->wr_data[reg_result_idx].control_curr;

            network_to_result(reg_result);

            trace_qemu_rdma_write_one_recvregres(block->remote_keys[chunk],
                                                 reg_result->rkey, chunk);

            block->remote_keys[chunk] = reg_result->rkey;
            block->remote_host_addr = reg_result->host_addr;
        } else {
            /* already registered before */
            if (qemu_rdma_register_and_get_keys(rdma, block, sge.addr,
                                                &sge.lkey, NULL, chunk,
                                                chunk_start, chunk_end)) {
                error_setg(errp, "cannot get lkey!");
                return -1;
            }
        }

        send_wr.wr.rdma.rkey = block->remote_keys[chunk];
    } else {
        send_wr.wr.rdma.rkey = block->remote_rkey;

        if (qemu_rdma_register_and_get_keys(rdma, block, sge.addr,
                                                     &sge.lkey, NULL, chunk,
                                                     chunk_start, chunk_end)) {
            error_setg(errp, "cannot get lkey!");
            return -1;
        }
    }

    /*
     * Encode the ram block index and chunk within this wrid.
     * We will use this information at the time of completion
     * to figure out which bitmap to check against and then which
     * chunk in the bitmap to look for.
     */
    send_wr.wr_id = qemu_rdma_make_wrid(RDMA_WRID_RDMA_WRITE,
                                        current_index, chunk);

    send_wr.opcode = IBV_WR_RDMA_WRITE;
    send_wr.send_flags = IBV_SEND_SIGNALED;
    send_wr.sg_list = &sge;
    send_wr.num_sge = 1;
    send_wr.wr.rdma.remote_addr = block->remote_host_addr +
                                (current_addr - block->offset);

    trace_qemu_rdma_write_one_post(chunk, sge.addr, send_wr.wr.rdma.remote_addr,
                                   sge.length);

    /*
     * ibv_post_send() does not return negative error numbers,
     * per the specification they are positive - no idea why.
     */
    ret = ibv_post_send(rdma->qp, &send_wr, &bad_wr);

    if (ret == ENOMEM) {
        trace_qemu_rdma_write_one_queue_full();
        ret = qemu_rdma_block_for_wrid(rdma, RDMA_WRID_RDMA_WRITE, NULL);
        if (ret < 0) {
            error_setg(errp, "rdma migration: failed to make "
                         "room in full send queue!");
            return -1;
        }

        goto retry;

    } else if (ret > 0) {
        error_setg_errno(errp, ret,
                         "rdma migration: post rdma write failed");
        return -1;
    }

    set_bit(chunk, block->transit_bitmap);
    stat64_add(&mig_stats.normal_pages, sge.length / qemu_target_page_size());
    /*
     * We are adding to transferred the amount of data written, but no
     * overhead at all.  I will assume that RDMA is magicaly and don't
     * need to transfer (at least) the addresses where it wants to
     * write the pages.  Here it looks like it should be something
     * like:
     *     sizeof(send_wr) + sge.length
     * but this being RDMA, who knows.
     */
    stat64_add(&mig_stats.rdma_bytes, sge.length);
    ram_transferred_add(sge.length);
    rdma->total_writes++;

    return 0;
}

/*
 * Push out any unwritten RDMA operations.
 *
 * We support sending out multiple chunks at the same time.
 * Not all of them need to get signaled in the completion queue.
 */
static int qemu_rdma_write_flush(RDMAContext *rdma, Error **errp)
{
    int ret;

    if (!rdma->current_length) {
        return 0;
    }

    ret = qemu_rdma_write_one(rdma, rdma->current_index, rdma->current_addr,
                              rdma->current_length, errp);

    if (ret < 0) {
        return -1;
    }

    if (ret == 0) {
        rdma->nb_sent++;
        trace_qemu_rdma_write_flush(rdma->nb_sent);
    }

    rdma->current_length = 0;
    rdma->current_addr = 0;

    return 0;
}

static inline bool qemu_rdma_buffer_mergeable(RDMAContext *rdma,
                    uint64_t offset, uint64_t len)
{
    RDMALocalBlock *block;
    uint8_t *host_addr;
    uint8_t *chunk_end;

    if (rdma->current_index < 0) {
        return false;
    }

    if (rdma->current_chunk < 0) {
        return false;
    }

    block = &(rdma->local_ram_blocks.block[rdma->current_index]);
    host_addr = block->local_host_addr + (offset - block->offset);
    chunk_end = ram_chunk_end(block, rdma->current_chunk);

    if (rdma->current_length == 0) {
        return false;
    }

    /*
     * Only merge into chunk sequentially.
     */
    if (offset != (rdma->current_addr + rdma->current_length)) {
        return false;
    }

    if (offset < block->offset) {
        return false;
    }

    if ((offset + len) > (block->offset + block->length)) {
        return false;
    }

    if ((host_addr + len) > chunk_end) {
        return false;
    }

    return true;
}

/*
 * We're not actually writing here, but doing three things:
 *
 * 1. Identify the chunk the buffer belongs to.
 * 2. If the chunk is full or the buffer doesn't belong to the current
 *    chunk, then start a new chunk and flush() the old chunk.
 * 3. To keep the hardware busy, we also group chunks into batches
 *    and only require that a batch gets acknowledged in the completion
 *    queue instead of each individual chunk.
 */
static int qemu_rdma_write(RDMAContext *rdma,
                           uint64_t block_offset, uint64_t offset,
                           uint64_t len, Error **errp)
{
    uint64_t current_addr = block_offset + offset;
    uint64_t index = rdma->current_index;
    uint64_t chunk = rdma->current_chunk;

    /* If we cannot merge it, we flush the current buffer first. */
    if (!qemu_rdma_buffer_mergeable(rdma, current_addr, len)) {
        if (qemu_rdma_write_flush(rdma, errp) < 0) {
            return -1;
        }
        rdma->current_length = 0;
        rdma->current_addr = current_addr;

        qemu_rdma_search_ram_block(rdma, block_offset,
                                   offset, len, &index, &chunk);
        rdma->current_index = index;
        rdma->current_chunk = chunk;
    }

    /* merge it */
    rdma->current_length += len;

    /* flush it if buffer is too large */
    if (rdma->current_length >= RDMA_MERGE_MAX) {
        return qemu_rdma_write_flush(rdma, errp);
    }

    return 0;
}

static void qemu_rdma_cleanup(RDMAContext *rdma)
{
    Error *err = NULL;

    if (rdma->cm_id && rdma->connected) {
        if ((rdma->errored ||
             migrate_get_current()->state == MIGRATION_STATUS_CANCELLING) &&
            !rdma->received_error) {
            RDMAControlHeader head = { .len = 0,
                                       .type = RDMA_CONTROL_ERROR,
                                       .repeat = 1,
                                     };
            warn_report("Early error. Sending error.");
            if (qemu_rdma_post_send_control(rdma, NULL, &head, &err) < 0) {
                warn_report_err(err);
            }
        }

        rdma_disconnect(rdma->cm_id);
        trace_qemu_rdma_cleanup_disconnect();
        rdma->connected = false;
    }

    if (rdma->channel) {
        qemu_set_fd_handler(rdma->channel->fd, NULL, NULL, NULL);
    }
    g_free(rdma->dest_blocks);
    rdma->dest_blocks = NULL;

    for (int i = 0; i < RDMA_WRID_MAX; i++) {
        if (rdma->wr_data[i].control_mr) {
            rdma->total_registrations--;
            ibv_dereg_mr(rdma->wr_data[i].control_mr);
        }
        rdma->wr_data[i].control_mr = NULL;
    }

    if (rdma->local_ram_blocks.block) {
        while (rdma->local_ram_blocks.nb_blocks) {
            rdma_delete_block(rdma, &rdma->local_ram_blocks.block[0]);
        }
    }

    if (rdma->qp) {
        rdma_destroy_qp(rdma->cm_id);
        rdma->qp = NULL;
    }
    if (rdma->recv_cq) {
        ibv_destroy_cq(rdma->recv_cq);
        rdma->recv_cq = NULL;
    }
    if (rdma->send_cq) {
        ibv_destroy_cq(rdma->send_cq);
        rdma->send_cq = NULL;
    }
    if (rdma->recv_comp_channel) {
        ibv_destroy_comp_channel(rdma->recv_comp_channel);
        rdma->recv_comp_channel = NULL;
    }
    if (rdma->send_comp_channel) {
        ibv_destroy_comp_channel(rdma->send_comp_channel);
        rdma->send_comp_channel = NULL;
    }
    if (rdma->pd) {
        ibv_dealloc_pd(rdma->pd);
        rdma->pd = NULL;
    }
    if (rdma->cm_id) {
        rdma_destroy_id(rdma->cm_id);
        rdma->cm_id = NULL;
    }

    /* the destination side, listen_id and channel is shared */
    if (rdma->listen_id) {
        if (!rdma->is_return_path) {
            rdma_destroy_id(rdma->listen_id);
        }
        rdma->listen_id = NULL;

        if (rdma->channel) {
            if (!rdma->is_return_path) {
                rdma_destroy_event_channel(rdma->channel);
            }
            rdma->channel = NULL;
        }
    }

    if (rdma->channel) {
        rdma_destroy_event_channel(rdma->channel);
        rdma->channel = NULL;
    }
    g_free(rdma->host);
    rdma->host = NULL;
}


static int qemu_rdma_source_init(RDMAContext *rdma, bool pin_all, Error **errp)
{
    int ret;

    /*
     * Will be validated against destination's actual capabilities
     * after the connect() completes.
     */
    rdma->pin_all = pin_all;

    ret = qemu_rdma_resolve_host(rdma, errp);
    if (ret < 0) {
        goto err_rdma_source_init;
    }

    ret = qemu_rdma_alloc_pd_cq(rdma, errp);
    if (ret < 0) {
        goto err_rdma_source_init;
    }

    ret = qemu_rdma_alloc_qp(rdma);
    if (ret < 0) {
        error_setg(errp, "RDMA ERROR: rdma migration: error allocating qp!");
        goto err_rdma_source_init;
    }

    qemu_rdma_init_ram_blocks(rdma);

    /* Build the hash that maps from offset to RAMBlock */
    rdma->blockmap = g_hash_table_new(g_direct_hash, g_direct_equal);
    for (int i = 0; i < rdma->local_ram_blocks.nb_blocks; i++) {
        g_hash_table_insert(rdma->blockmap,
                (void *)(uintptr_t)rdma->local_ram_blocks.block[i].offset,
                &rdma->local_ram_blocks.block[i]);
    }

    for (int i = 0; i < RDMA_WRID_MAX; i++) {
        ret = qemu_rdma_reg_control(rdma, i);
        if (ret < 0) {
            error_setg(errp, "RDMA ERROR: rdma migration: error "
                       "registering %d control!", i);
            goto err_rdma_source_init;
        }
    }

    return 0;

err_rdma_source_init:
    qemu_rdma_cleanup(rdma);
    return -1;
}

static int qemu_get_cm_event_timeout(RDMAContext *rdma,
                                     struct rdma_cm_event **cm_event,
                                     long msec, Error **errp)
{
    int ret;
    struct pollfd poll_fd = {
                                .fd = rdma->channel->fd,
                                .events = POLLIN,
                                .revents = 0
                            };

    do {
        ret = poll(&poll_fd, 1, msec);
    } while (ret < 0 && errno == EINTR);

    if (ret == 0) {
        error_setg(errp, "RDMA ERROR: poll cm event timeout");
        return -1;
    } else if (ret < 0) {
        error_setg(errp, "RDMA ERROR: failed to poll cm event, errno=%i",
                   errno);
        return -1;
    } else if (poll_fd.revents & POLLIN) {
        if (rdma_get_cm_event(rdma->channel, cm_event) < 0) {
            error_setg(errp, "RDMA ERROR: failed to get cm event");
            return -1;
        }
        return 0;
    } else {
        error_setg(errp, "RDMA ERROR: no POLLIN event, revent=%x",
                   poll_fd.revents);
        return -1;
    }
}

static int qemu_rdma_connect(RDMAContext *rdma, bool return_path,
                             Error **errp)
{
    RDMACapabilities cap = {
                                .version = RDMA_CONTROL_VERSION_CURRENT,
                                .flags = 0,
                           };
    struct rdma_conn_param conn_param = { .initiator_depth = 2,
                                          .retry_count = 5,
                                          .private_data = &cap,
                                          .private_data_len = sizeof(cap),
                                        };
    struct rdma_cm_event *cm_event;
    int ret;

    /*
     * Only negotiate the capability with destination if the user
     * on the source first requested the capability.
     */
    if (rdma->pin_all) {
        trace_qemu_rdma_connect_pin_all_requested();
        cap.flags |= RDMA_CAPABILITY_PIN_ALL;
    }

    caps_to_network(&cap);

    ret = qemu_rdma_post_recv_control(rdma, RDMA_WRID_READY, errp);
    if (ret < 0) {
        goto err_rdma_source_connect;
    }

    ret = rdma_connect(rdma->cm_id, &conn_param);
    if (ret < 0) {
        error_setg_errno(errp, errno,
                         "RDMA ERROR: connecting to destination!");
        goto err_rdma_source_connect;
    }

    if (return_path) {
        ret = qemu_get_cm_event_timeout(rdma, &cm_event, 5000, errp);
    } else {
        ret = rdma_get_cm_event(rdma->channel, &cm_event);
        if (ret < 0) {
            error_setg_errno(errp, errno,
                             "RDMA ERROR: failed to get cm event");
        }
    }
    if (ret < 0) {
        goto err_rdma_source_connect;
    }

    if (cm_event->event != RDMA_CM_EVENT_ESTABLISHED) {
        error_setg(errp, "RDMA ERROR: connecting to destination!");
        rdma_ack_cm_event(cm_event);
        goto err_rdma_source_connect;
    }
    rdma->connected = true;

    memcpy(&cap, cm_event->param.conn.private_data, sizeof(cap));
    network_to_caps(&cap);

    /*
     * Verify that the *requested* capabilities are supported by the destination
     * and disable them otherwise.
     */
    if (rdma->pin_all && !(cap.flags & RDMA_CAPABILITY_PIN_ALL)) {
        warn_report("RDMA: Server cannot support pinning all memory. "
                    "Will register memory dynamically.");
        rdma->pin_all = false;
    }

    trace_qemu_rdma_connect_pin_all_outcome(rdma->pin_all);

    rdma_ack_cm_event(cm_event);

    rdma->control_ready_expected = 1;
    rdma->nb_sent = 0;
    return 0;

err_rdma_source_connect:
    qemu_rdma_cleanup(rdma);
    return -1;
}

static int qemu_rdma_dest_init(RDMAContext *rdma, Error **errp)
{
    Error *err = NULL;
    int ret;
    struct rdma_cm_id *listen_id;
    char ip[40] = "unknown";
    struct rdma_addrinfo *res, *e;
    char port_str[16];
    int reuse = 1;

    for (int i = 0; i < RDMA_WRID_MAX; i++) {
        rdma->wr_data[i].control_len = 0;
        rdma->wr_data[i].control_curr = NULL;
    }

    if (!rdma->host || !rdma->host[0]) {
        error_setg(errp, "RDMA ERROR: RDMA host is not set!");
        rdma->errored = true;
        return -1;
    }
    /* create CM channel */
    rdma->channel = rdma_create_event_channel();
    if (!rdma->channel) {
        error_setg(errp, "RDMA ERROR: could not create rdma event channel");
        rdma->errored = true;
        return -1;
    }

    /* create CM id */
    ret = rdma_create_id(rdma->channel, &listen_id, NULL, RDMA_PS_TCP);
    if (ret < 0) {
        error_setg(errp, "RDMA ERROR: could not create cm_id!");
        goto err_dest_init_create_listen_id;
    }

    snprintf(port_str, 16, "%d", rdma->port);
    port_str[15] = '\0';

    ret = rdma_getaddrinfo(rdma->host, port_str, NULL, &res);
    if (ret) {
        error_setg(errp, "RDMA ERROR: could not rdma_getaddrinfo address %s",
                   rdma->host);
        goto err_dest_init_bind_addr;
    }

    ret = rdma_set_option(listen_id, RDMA_OPTION_ID, RDMA_OPTION_ID_REUSEADDR,
                          &reuse, sizeof reuse);
    if (ret < 0) {
        error_setg(errp, "RDMA ERROR: Error: could not set REUSEADDR option");
        goto err_dest_init_bind_addr;
    }

    /* Try all addresses, saving the first error in @err */
    for (e = res; e != NULL; e = e->ai_next) {
        Error **local_errp = err ? NULL : &err;

        inet_ntop(e->ai_family,
            &((struct sockaddr_in *) e->ai_dst_addr)->sin_addr, ip, sizeof ip);
        trace_qemu_rdma_dest_init_trying(rdma->host, ip);
        ret = rdma_bind_addr(listen_id, e->ai_dst_addr);
        if (ret < 0) {
            continue;
        }
        if (e->ai_family == AF_INET6) {
            ret = qemu_rdma_broken_ipv6_kernel(listen_id->verbs,
                                               local_errp);
            if (ret < 0) {
                continue;
            }
        }
        error_free(err);
        break;
    }

    rdma_freeaddrinfo(res);
    if (!e) {
        if (err) {
            error_propagate(errp, err);
        } else {
            error_setg(errp, "RDMA ERROR: Error: could not rdma_bind_addr!");
        }
        goto err_dest_init_bind_addr;
    }

    rdma->listen_id = listen_id;
    qemu_rdma_dump_gid("dest_init", listen_id);
    return 0;

err_dest_init_bind_addr:
    rdma_destroy_id(listen_id);
err_dest_init_create_listen_id:
    rdma_destroy_event_channel(rdma->channel);
    rdma->channel = NULL;
    rdma->errored = true;
    return -1;

}

static void qemu_rdma_return_path_dest_init(RDMAContext *rdma_return_path,
                                            RDMAContext *rdma)
{
    for (int i = 0; i < RDMA_WRID_MAX; i++) {
        rdma_return_path->wr_data[i].control_len = 0;
        rdma_return_path->wr_data[i].control_curr = NULL;
    }

    /*the CM channel and CM id is shared*/
    rdma_return_path->channel = rdma->channel;
    rdma_return_path->listen_id = rdma->listen_id;

    rdma->return_path = rdma_return_path;
    rdma_return_path->return_path = rdma;
    rdma_return_path->is_return_path = true;
}

static RDMAContext *qemu_rdma_data_init(InetSocketAddress *saddr, Error **errp)
{
    RDMAContext *rdma = NULL;

    rdma = g_new0(RDMAContext, 1);
    rdma->current_index = -1;
    rdma->current_chunk = -1;

    rdma->host = g_strdup(saddr->host);
    rdma->port = atoi(saddr->port);
    return rdma;
}

/*
 * QEMUFile interface to the control channel.
 * SEND messages for control only.
 * VM's ram is handled with regular RDMA messages.
 */
static ssize_t qio_channel_rdma_writev(QIOChannel *ioc,
                                       const struct iovec *iov,
                                       size_t niov,
                                       int *fds,
                                       size_t nfds,
                                       int flags,
                                       Error **errp)
{
    QIOChannelRDMA *rioc = QIO_CHANNEL_RDMA(ioc);
    RDMAContext *rdma;
    int ret;
    ssize_t done = 0;
    size_t len;

    RCU_READ_LOCK_GUARD();
    rdma = qatomic_rcu_read(&rioc->rdmaout);

    if (!rdma) {
        error_setg(errp, "RDMA control channel output is not set");
        return -1;
    }

    if (rdma->errored) {
        error_setg(errp,
                   "RDMA is in an error state waiting migration to abort!");
        return -1;
    }

    /*
     * Push out any writes that
     * we're queued up for VM's ram.
     */
    ret = qemu_rdma_write_flush(rdma, errp);
    if (ret < 0) {
        rdma->errored = true;
        return -1;
    }

    for (int i = 0; i < niov; i++) {
        size_t remaining = iov[i].iov_len;
        uint8_t * data = (void *)iov[i].iov_base;
        while (remaining) {
            RDMAControlHeader head = {};

            len = MIN(remaining, RDMA_SEND_INCREMENT);
            remaining -= len;

            head.len = len;
            head.type = RDMA_CONTROL_QEMU_FILE;

            ret = qemu_rdma_exchange_send(rdma, &head,
                                          data, NULL, NULL, NULL, errp);

            if (ret < 0) {
                rdma->errored = true;
                return -1;
            }

            data += len;
            done += len;
        }
    }

    return done;
}

static size_t qemu_rdma_fill(RDMAContext *rdma, uint8_t *buf,
                             size_t size, int idx)
{
    size_t len = 0;

    if (rdma->wr_data[idx].control_len) {
        trace_qemu_rdma_fill(rdma->wr_data[idx].control_len, size);

        len = MIN(size, rdma->wr_data[idx].control_len);
        memcpy(buf, rdma->wr_data[idx].control_curr, len);
        rdma->wr_data[idx].control_curr += len;
        rdma->wr_data[idx].control_len -= len;
    }

    return len;
}

/*
 * QEMUFile interface to the control channel.
 * RDMA links don't use bytestreams, so we have to
 * return bytes to QEMUFile opportunistically.
 */
static ssize_t qio_channel_rdma_readv(QIOChannel *ioc,
                                      const struct iovec *iov,
                                      size_t niov,
                                      int **fds,
                                      size_t *nfds,
                                      int flags,
                                      Error **errp)
{
    QIOChannelRDMA *rioc = QIO_CHANNEL_RDMA(ioc);
    RDMAContext *rdma;
    RDMAControlHeader head;
    int ret;
    ssize_t done = 0;
    size_t len;

    RCU_READ_LOCK_GUARD();
    rdma = qatomic_rcu_read(&rioc->rdmain);

    if (!rdma) {
        error_setg(errp, "RDMA control channel input is not set");
        return -1;
    }

    if (rdma->errored) {
        error_setg(errp,
                   "RDMA is in an error state waiting migration to abort!");
        return -1;
    }

    for (int i = 0; i < niov; i++) {
        size_t want = iov[i].iov_len;
        uint8_t *data = (void *)iov[i].iov_base;

        /*
         * First, we hold on to the last SEND message we
         * were given and dish out the bytes until we run
         * out of bytes.
         */
        len = qemu_rdma_fill(rdma, data, want, 0);
        done += len;
        want -= len;
        /* Got what we needed, so go to next iovec */
        if (want == 0) {
            continue;
        }

        /* If we got any data so far, then don't wait
         * for more, just return what we have */
        if (done > 0) {
            break;
        }


        /* We've got nothing at all, so lets wait for
         * more to arrive
         */
        ret = qemu_rdma_exchange_recv(rdma, &head, RDMA_CONTROL_QEMU_FILE,
                                      errp);

        if (ret < 0) {
            rdma->errored = true;
            return -1;
        }

        /*
         * SEND was received with new bytes, now try again.
         */
        len = qemu_rdma_fill(rdma, data, want, 0);
        done += len;
        want -= len;

        /* Still didn't get enough, so lets just return */
        if (want) {
            if (done == 0) {
                return QIO_CHANNEL_ERR_BLOCK;
            } else {
                break;
            }
        }
    }
    return done;
}

/*
 * Block until all the outstanding chunks have been delivered by the hardware.
 */
static int qemu_rdma_drain_cq(RDMAContext *rdma)
{
    Error *err = NULL;

    if (qemu_rdma_write_flush(rdma, &err) < 0) {
        error_report_err(err);
        return -1;
    }

    while (rdma->nb_sent) {
        if (qemu_rdma_block_for_wrid(rdma, RDMA_WRID_RDMA_WRITE, NULL) < 0) {
            error_report("rdma migration: complete polling error!");
            return -1;
        }
    }

    qemu_rdma_unregister_waiting(rdma);

    return 0;
}


static int qio_channel_rdma_set_blocking(QIOChannel *ioc,
                                         bool blocking,
                                         Error **errp)
{
    QIOChannelRDMA *rioc = QIO_CHANNEL_RDMA(ioc);
    /* XXX we should make readv/writev actually honour this :-) */
    rioc->blocking = blocking;
    return 0;
}


typedef struct QIOChannelRDMASource QIOChannelRDMASource;
struct QIOChannelRDMASource {
    GSource parent;
    QIOChannelRDMA *rioc;
    GIOCondition condition;
};

static gboolean
qio_channel_rdma_source_prepare(GSource *source,
                                gint *timeout)
{
    QIOChannelRDMASource *rsource = (QIOChannelRDMASource *)source;
    RDMAContext *rdma;
    GIOCondition cond = 0;
    *timeout = -1;

    RCU_READ_LOCK_GUARD();
    if (rsource->condition == G_IO_IN) {
        rdma = qatomic_rcu_read(&rsource->rioc->rdmain);
    } else {
        rdma = qatomic_rcu_read(&rsource->rioc->rdmaout);
    }

    if (!rdma) {
        error_report("RDMAContext is NULL when prepare Gsource");
        return FALSE;
    }

    if (rdma->wr_data[0].control_len) {
        cond |= G_IO_IN;
    }
    cond |= G_IO_OUT;

    return cond & rsource->condition;
}

static gboolean
qio_channel_rdma_source_check(GSource *source)
{
    QIOChannelRDMASource *rsource = (QIOChannelRDMASource *)source;
    RDMAContext *rdma;
    GIOCondition cond = 0;

    RCU_READ_LOCK_GUARD();
    if (rsource->condition == G_IO_IN) {
        rdma = qatomic_rcu_read(&rsource->rioc->rdmain);
    } else {
        rdma = qatomic_rcu_read(&rsource->rioc->rdmaout);
    }

    if (!rdma) {
        error_report("RDMAContext is NULL when check Gsource");
        return FALSE;
    }

    if (rdma->wr_data[0].control_len) {
        cond |= G_IO_IN;
    }
    cond |= G_IO_OUT;

    return cond & rsource->condition;
}

static gboolean
qio_channel_rdma_source_dispatch(GSource *source,
                                 GSourceFunc callback,
                                 gpointer user_data)
{
    QIOChannelFunc func = (QIOChannelFunc)callback;
    QIOChannelRDMASource *rsource = (QIOChannelRDMASource *)source;
    RDMAContext *rdma;
    GIOCondition cond = 0;

    RCU_READ_LOCK_GUARD();
    if (rsource->condition == G_IO_IN) {
        rdma = qatomic_rcu_read(&rsource->rioc->rdmain);
    } else {
        rdma = qatomic_rcu_read(&rsource->rioc->rdmaout);
    }

    if (!rdma) {
        error_report("RDMAContext is NULL when dispatch Gsource");
        return FALSE;
    }

    if (rdma->wr_data[0].control_len) {
        cond |= G_IO_IN;
    }
    cond |= G_IO_OUT;

    return (*func)(QIO_CHANNEL(rsource->rioc),
                   (cond & rsource->condition),
                   user_data);
}

static void
qio_channel_rdma_source_finalize(GSource *source)
{
    QIOChannelRDMASource *ssource = (QIOChannelRDMASource *)source;

    object_unref(OBJECT(ssource->rioc));
}

static GSourceFuncs qio_channel_rdma_source_funcs = {
    qio_channel_rdma_source_prepare,
    qio_channel_rdma_source_check,
    qio_channel_rdma_source_dispatch,
    qio_channel_rdma_source_finalize
};

static GSource *qio_channel_rdma_create_watch(QIOChannel *ioc,
                                              GIOCondition condition)
{
    QIOChannelRDMA *rioc = QIO_CHANNEL_RDMA(ioc);
    QIOChannelRDMASource *ssource;
    GSource *source;

    source = g_source_new(&qio_channel_rdma_source_funcs,
                          sizeof(QIOChannelRDMASource));
    ssource = (QIOChannelRDMASource *)source;

    ssource->rioc = rioc;
    object_ref(OBJECT(rioc));

    ssource->condition = condition;

    return source;
}

static void qio_channel_rdma_set_aio_fd_handler(QIOChannel *ioc,
                                                AioContext *read_ctx,
                                                IOHandler *io_read,
                                                AioContext *write_ctx,
                                                IOHandler *io_write,
                                                void *opaque)
{
    QIOChannelRDMA *rioc = QIO_CHANNEL_RDMA(ioc);
    if (io_read) {
        aio_set_fd_handler(read_ctx, rioc->rdmain->recv_comp_channel->fd,
                           io_read, io_write, NULL, NULL, opaque);
        aio_set_fd_handler(read_ctx, rioc->rdmain->send_comp_channel->fd,
                           io_read, io_write, NULL, NULL, opaque);
    } else {
        aio_set_fd_handler(write_ctx, rioc->rdmaout->recv_comp_channel->fd,
                           io_read, io_write, NULL, NULL, opaque);
        aio_set_fd_handler(write_ctx, rioc->rdmaout->send_comp_channel->fd,
                           io_read, io_write, NULL, NULL, opaque);
    }
}

struct rdma_close_rcu {
    struct rcu_head rcu;
    RDMAContext *rdmain;
    RDMAContext *rdmaout;
};

/* callback from qio_channel_rdma_close via call_rcu */
static void qio_channel_rdma_close_rcu(struct rdma_close_rcu *rcu)
{
    if (rcu->rdmain) {
        qemu_rdma_cleanup(rcu->rdmain);
    }

    if (rcu->rdmaout) {
        qemu_rdma_cleanup(rcu->rdmaout);
    }

    g_free(rcu->rdmain);
    g_free(rcu->rdmaout);
    g_free(rcu);
}

static int qio_channel_rdma_close(QIOChannel *ioc,
                                  Error **errp)
{
    QIOChannelRDMA *rioc = QIO_CHANNEL_RDMA(ioc);
    RDMAContext *rdmain, *rdmaout;
    struct rdma_close_rcu *rcu = g_new(struct rdma_close_rcu, 1);

    trace_qemu_rdma_close();

    rdmain = rioc->rdmain;
    if (rdmain) {
        qatomic_rcu_set(&rioc->rdmain, NULL);
    }

    rdmaout = rioc->rdmaout;
    if (rdmaout) {
        qatomic_rcu_set(&rioc->rdmaout, NULL);
    }

    rcu->rdmain = rdmain;
    rcu->rdmaout = rdmaout;
    call_rcu(rcu, qio_channel_rdma_close_rcu, rcu);

    return 0;
}

static int
qio_channel_rdma_shutdown(QIOChannel *ioc,
                            QIOChannelShutdown how,
                            Error **errp)
{
    QIOChannelRDMA *rioc = QIO_CHANNEL_RDMA(ioc);
    RDMAContext *rdmain, *rdmaout;

    RCU_READ_LOCK_GUARD();

    rdmain = qatomic_rcu_read(&rioc->rdmain);
    rdmaout = qatomic_rcu_read(&rioc->rdmain);

    switch (how) {
    case QIO_CHANNEL_SHUTDOWN_READ:
        if (rdmain) {
            rdmain->errored = true;
        }
        break;
    case QIO_CHANNEL_SHUTDOWN_WRITE:
        if (rdmaout) {
            rdmaout->errored = true;
        }
        break;
    case QIO_CHANNEL_SHUTDOWN_BOTH:
    default:
        if (rdmain) {
            rdmain->errored = true;
        }
        if (rdmaout) {
            rdmaout->errored = true;
        }
        break;
    }

    return 0;
}

/*
 * Parameters:
 *    @offset == 0 :
 *        This means that 'block_offset' is a full virtual address that does not
 *        belong to a RAMBlock of the virtual machine and instead
 *        represents a private malloc'd memory area that the caller wishes to
 *        transfer.
 *
 *    @offset != 0 :
 *        Offset is an offset to be added to block_offset and used
 *        to also lookup the corresponding RAMBlock.
 *
 *    @size : Number of bytes to transfer
 *
 *    @pages_sent : User-specificed pointer to indicate how many pages were
 *                  sent. Usually, this will not be more than a few bytes of
 *                  the protocol because most transfers are sent asynchronously.
 */
static int qemu_rdma_save_page(QEMUFile *f, ram_addr_t block_offset,
                               ram_addr_t offset, size_t size)
{
    QIOChannelRDMA *rioc = QIO_CHANNEL_RDMA(qemu_file_get_ioc(f));
    Error *err = NULL;
    RDMAContext *rdma;
    int ret;

    RCU_READ_LOCK_GUARD();
    rdma = qatomic_rcu_read(&rioc->rdmaout);

    if (!rdma) {
        return -1;
    }

    if (rdma_errored(rdma)) {
        return -1;
    }

    qemu_fflush(f);

    /*
     * Add this page to the current 'chunk'. If the chunk
     * is full, or the page doesn't belong to the current chunk,
     * an actual RDMA write will occur and a new chunk will be formed.
     */
    ret = qemu_rdma_write(rdma, block_offset, offset, size, &err);
    if (ret < 0) {
        error_report_err(err);
        goto err;
    }

    /*
     * Drain the Completion Queue if possible, but do not block,
     * just poll.
     *
     * If nothing to poll, the end of the iteration will do this
     * again to make sure we don't overflow the request queue.
     */
    while (1) {
        uint64_t wr_id, wr_id_in;
        ret = qemu_rdma_poll(rdma, rdma->recv_cq, &wr_id_in, NULL);

        if (ret < 0) {
            error_report("rdma migration: polling error");
            goto err;
        }

        wr_id = wr_id_in & RDMA_WRID_TYPE_MASK;

        if (wr_id == RDMA_WRID_NONE) {
            break;
        }
    }

    while (1) {
        uint64_t wr_id, wr_id_in;
        ret = qemu_rdma_poll(rdma, rdma->send_cq, &wr_id_in, NULL);

        if (ret < 0) {
            error_report("rdma migration: polling error");
            goto err;
        }

        wr_id = wr_id_in & RDMA_WRID_TYPE_MASK;

        if (wr_id == RDMA_WRID_NONE) {
            break;
        }
    }

    return RAM_SAVE_CONTROL_DELAYED;

err:
    rdma->errored = true;
    return -1;
}

int rdma_control_save_page(QEMUFile *f, ram_addr_t block_offset,
                           ram_addr_t offset, size_t size)
{
    if (!migrate_rdma() || migration_in_postcopy()) {
        return RAM_SAVE_CONTROL_NOT_SUPP;
    }

    int ret = qemu_rdma_save_page(f, block_offset, offset, size);

    if (ret != RAM_SAVE_CONTROL_DELAYED &&
        ret != RAM_SAVE_CONTROL_NOT_SUPP) {
        if (ret < 0) {
            qemu_file_set_error(f, ret);
        }
    }
    return ret;
}

static void rdma_accept_incoming_migration(void *opaque);

static void rdma_cm_poll_handler(void *opaque)
{
    RDMAContext *rdma = opaque;
    struct rdma_cm_event *cm_event;
    MigrationIncomingState *mis = migration_incoming_get_current();

    if (rdma_get_cm_event(rdma->channel, &cm_event) < 0) {
        error_report("get_cm_event failed %d", errno);
        return;
    }

    if (cm_event->event == RDMA_CM_EVENT_DISCONNECTED ||
        cm_event->event == RDMA_CM_EVENT_DEVICE_REMOVAL) {
        if (!rdma->errored &&
            migration_incoming_get_current()->state !=
              MIGRATION_STATUS_COMPLETED) {
            error_report("receive cm event, cm event is %d", cm_event->event);
            rdma->errored = true;
            if (rdma->return_path) {
                rdma->return_path->errored = true;
            }
        }
        rdma_ack_cm_event(cm_event);
        if (mis->loadvm_co) {
            qemu_coroutine_enter(mis->loadvm_co);
        }
        return;
    }
    rdma_ack_cm_event(cm_event);
}

static int qemu_rdma_accept(RDMAContext *rdma)
{
    Error *err = NULL;
    RDMACapabilities cap;
    struct rdma_conn_param conn_param = {
                                            .responder_resources = 2,
                                            .private_data = &cap,
                                            .private_data_len = sizeof(cap),
                                         };
    RDMAContext *rdma_return_path = NULL;
    g_autoptr(InetSocketAddress) isock = g_new0(InetSocketAddress, 1);
    struct rdma_cm_event *cm_event;
    struct ibv_context *verbs;
    int ret;

    ret = rdma_get_cm_event(rdma->channel, &cm_event);
    if (ret < 0) {
        goto err_rdma_dest_wait;
    }

    if (cm_event->event != RDMA_CM_EVENT_CONNECT_REQUEST) {
        rdma_ack_cm_event(cm_event);
        goto err_rdma_dest_wait;
    }

    isock->host = rdma->host;
    isock->port = g_strdup_printf("%d", rdma->port);

    /*
     * initialize the RDMAContext for return path for postcopy after first
     * connection request reached.
     */
    if ((migrate_postcopy() || migrate_return_path())
        && !rdma->is_return_path) {
        rdma_return_path = qemu_rdma_data_init(isock, NULL);
        if (rdma_return_path == NULL) {
            rdma_ack_cm_event(cm_event);
            goto err_rdma_dest_wait;
        }

        qemu_rdma_return_path_dest_init(rdma_return_path, rdma);
    }

    memcpy(&cap, cm_event->param.conn.private_data, sizeof(cap));

    network_to_caps(&cap);

    if (cap.version < 1 || cap.version > RDMA_CONTROL_VERSION_CURRENT) {
        error_report("Unknown source RDMA version: %d, bailing...",
                     cap.version);
        rdma_ack_cm_event(cm_event);
        goto err_rdma_dest_wait;
    }

    /*
     * Respond with only the capabilities this version of QEMU knows about.
     */
    cap.flags &= known_capabilities;

    /*
     * Enable the ones that we do know about.
     * Add other checks here as new ones are introduced.
     */
    if (cap.flags & RDMA_CAPABILITY_PIN_ALL) {
        rdma->pin_all = true;
    }

    rdma->cm_id = cm_event->id;
    verbs = cm_event->id->verbs;

    rdma_ack_cm_event(cm_event);

    trace_qemu_rdma_accept_pin_state(rdma->pin_all);

    caps_to_network(&cap);

    trace_qemu_rdma_accept_pin_verbsc(verbs);

    if (!rdma->verbs) {
        rdma->verbs = verbs;
    } else if (rdma->verbs != verbs) {
        error_report("ibv context not matching %p, %p!", rdma->verbs,
                     verbs);
        goto err_rdma_dest_wait;
    }

    qemu_rdma_dump_id("dest_init", verbs);

    ret = qemu_rdma_alloc_pd_cq(rdma, &err);
    if (ret < 0) {
        error_report_err(err);
        goto err_rdma_dest_wait;
    }

    ret = qemu_rdma_alloc_qp(rdma);
    if (ret < 0) {
        error_report("rdma migration: error allocating qp!");
        goto err_rdma_dest_wait;
    }

    qemu_rdma_init_ram_blocks(rdma);

    for (int i = 0; i < RDMA_WRID_MAX; i++) {
        ret = qemu_rdma_reg_control(rdma, i);
        if (ret < 0) {
            error_report("rdma: error registering %d control", i);
            goto err_rdma_dest_wait;
        }
    }

    /* Accept the second connection request for return path */
    if ((migrate_postcopy() || migrate_return_path())
        && !rdma->is_return_path) {
        qemu_set_fd_handler(rdma->channel->fd, rdma_accept_incoming_migration,
                            NULL,
                            (void *)(intptr_t)rdma->return_path);
    } else {
        qemu_set_fd_handler(rdma->channel->fd, rdma_cm_poll_handler,
                            NULL, rdma);
    }

    ret = rdma_accept(rdma->cm_id, &conn_param);
    if (ret < 0) {
        error_report("rdma_accept failed");
        goto err_rdma_dest_wait;
    }

    ret = rdma_get_cm_event(rdma->channel, &cm_event);
    if (ret < 0) {
        error_report("rdma_accept get_cm_event failed");
        goto err_rdma_dest_wait;
    }

    if (cm_event->event != RDMA_CM_EVENT_ESTABLISHED) {
        error_report("rdma_accept not event established");
        rdma_ack_cm_event(cm_event);
        goto err_rdma_dest_wait;
    }

    rdma_ack_cm_event(cm_event);
    rdma->connected = true;

    ret = qemu_rdma_post_recv_control(rdma, RDMA_WRID_READY, &err);
    if (ret < 0) {
        error_report_err(err);
        goto err_rdma_dest_wait;
    }

    qemu_rdma_dump_gid("dest_connect", rdma->cm_id);

    return 0;

err_rdma_dest_wait:
    rdma->errored = true;
    qemu_rdma_cleanup(rdma);
    g_free(rdma_return_path);
    return -1;
}

static int dest_ram_sort_func(const void *a, const void *b)
{
    unsigned int a_index = ((const RDMALocalBlock *)a)->src_index;
    unsigned int b_index = ((const RDMALocalBlock *)b)->src_index;

    return (a_index < b_index) ? -1 : (a_index != b_index);
}

/*
 * During each iteration of the migration, we listen for instructions
 * by the source VM to perform dynamic page registrations before they
 * can perform RDMA operations.
 *
 * We respond with the 'rkey'.
 *
 * Keep doing this until the source tells us to stop.
 */
int rdma_registration_handle(QEMUFile *f)
{
    RDMAControlHeader reg_resp = { .len = sizeof(RDMARegisterResult),
                               .type = RDMA_CONTROL_REGISTER_RESULT,
                               .repeat = 0,
                             };
    RDMAControlHeader unreg_resp = { .len = 0,
                               .type = RDMA_CONTROL_UNREGISTER_FINISHED,
                               .repeat = 0,
                             };
    RDMAControlHeader blocks = { .type = RDMA_CONTROL_RAM_BLOCKS_RESULT,
                                 .repeat = 1 };
    QIOChannelRDMA *rioc;
    Error *err = NULL;
    RDMAContext *rdma;
    RDMALocalBlocks *local;
    RDMAControlHeader head;
    RDMARegister *reg, *registers;
    RDMACompress *comp;
    RDMARegisterResult *reg_result;
    static RDMARegisterResult results[RDMA_CONTROL_MAX_COMMANDS_PER_MESSAGE];
    RDMALocalBlock *block;
    void *host_addr;
    int ret;
    int idx = 0;

    if (!migrate_rdma()) {
        return 0;
    }

    RCU_READ_LOCK_GUARD();
    rioc = QIO_CHANNEL_RDMA(qemu_file_get_ioc(f));
    rdma = qatomic_rcu_read(&rioc->rdmain);

    if (!rdma) {
        return -1;
    }

    if (rdma_errored(rdma)) {
        return -1;
    }

    local = &rdma->local_ram_blocks;
    do {
        trace_rdma_registration_handle_wait();

        ret = qemu_rdma_exchange_recv(rdma, &head, RDMA_CONTROL_NONE, &err);

        if (ret < 0) {
            error_report_err(err);
            break;
        }

        if (head.repeat > RDMA_CONTROL_MAX_COMMANDS_PER_MESSAGE) {
            error_report("rdma: Too many requests in this message (%d)."
                            "Bailing.", head.repeat);
            break;
        }

        switch (head.type) {
        case RDMA_CONTROL_COMPRESS:
            comp = (RDMACompress *) rdma->wr_data[idx].control_curr;
            network_to_compress(comp);

            trace_rdma_registration_handle_compress(comp->length,
                                                    comp->block_idx,
                                                    comp->offset);
            if (comp->block_idx >= rdma->local_ram_blocks.nb_blocks) {
                error_report("rdma: 'compress' bad block index %u (vs %d)",
                             (unsigned int)comp->block_idx,
                             rdma->local_ram_blocks.nb_blocks);
                goto err;
            }
            block = &(rdma->local_ram_blocks.block[comp->block_idx]);

            host_addr = block->local_host_addr +
                            (comp->offset - block->offset);
            if (comp->value) {
                error_report("rdma: Zero page with non-zero (%d) value",
                             comp->value);
                goto err;
            }
            ram_handle_zero(host_addr, comp->length);
            break;

        case RDMA_CONTROL_REGISTER_FINISHED:
            trace_rdma_registration_handle_finished();
            return 0;

        case RDMA_CONTROL_RAM_BLOCKS_REQUEST:
            trace_rdma_registration_handle_ram_blocks();

            /* Sort our local RAM Block list so it's the same as the source,
             * we can do this since we've filled in a src_index in the list
             * as we received the RAMBlock list earlier.
             */
            qsort(rdma->local_ram_blocks.block,
                  rdma->local_ram_blocks.nb_blocks,
                  sizeof(RDMALocalBlock), dest_ram_sort_func);
            for (int i = 0; i < local->nb_blocks; i++) {
                local->block[i].index = i;
            }

            if (rdma->pin_all) {
                ret = qemu_rdma_reg_whole_ram_blocks(rdma, &err);
                if (ret < 0) {
                    error_report_err(err);
                    goto err;
                }
            }

            /*
             * Dest uses this to prepare to transmit the RAMBlock descriptions
             * to the source VM after connection setup.
             * Both sides use the "remote" structure to communicate and update
             * their "local" descriptions with what was sent.
             */
            for (int i = 0; i < local->nb_blocks; i++) {
                rdma->dest_blocks[i].remote_host_addr =
                    (uintptr_t)(local->block[i].local_host_addr);

                if (rdma->pin_all) {
                    rdma->dest_blocks[i].remote_rkey = local->block[i].mr->rkey;
                }

                rdma->dest_blocks[i].offset = local->block[i].offset;
                rdma->dest_blocks[i].length = local->block[i].length;

                dest_block_to_network(&rdma->dest_blocks[i]);
                trace_rdma_registration_handle_ram_blocks_loop(
                    local->block[i].block_name,
                    local->block[i].offset,
                    local->block[i].length,
                    local->block[i].local_host_addr,
                    local->block[i].src_index);
            }

            blocks.len = rdma->local_ram_blocks.nb_blocks
                                                * sizeof(RDMADestBlock);


            ret = qemu_rdma_post_send_control(rdma,
                                    (uint8_t *) rdma->dest_blocks, &blocks,
                                    &err);

            if (ret < 0) {
                error_report_err(err);
                goto err;
            }

            break;
        case RDMA_CONTROL_REGISTER_REQUEST:
            trace_rdma_registration_handle_register(head.repeat);

            reg_resp.repeat = head.repeat;
            registers = (RDMARegister *) rdma->wr_data[idx].control_curr;

            for (int count = 0; count < head.repeat; count++) {
                uint64_t chunk;
                uint8_t *chunk_start, *chunk_end;

                reg = &registers[count];
                network_to_register(reg);

                reg_result = &results[count];

                trace_rdma_registration_handle_register_loop(count,
                         reg->current_index, reg->key.current_addr, reg->chunks);

                if (reg->current_index >= rdma->local_ram_blocks.nb_blocks) {
                    error_report("rdma: 'register' bad block index %u (vs %d)",
                                 (unsigned int)reg->current_index,
                                 rdma->local_ram_blocks.nb_blocks);
                    goto err;
                }
                block = &(rdma->local_ram_blocks.block[reg->current_index]);
                if (block->is_ram_block) {
                    if (block->offset > reg->key.current_addr) {
                        error_report("rdma: bad register address for block %s"
                            " offset: %" PRIx64 " current_addr: %" PRIx64,
                            block->block_name, block->offset,
                            reg->key.current_addr);
                        goto err;
                    }
                    host_addr = (block->local_host_addr +
                                (reg->key.current_addr - block->offset));
                    chunk = ram_chunk_index(block->local_host_addr,
                                            (uint8_t *) host_addr);
                } else {
                    chunk = reg->key.chunk;
                    host_addr = block->local_host_addr +
                        (reg->key.chunk * (1UL << RDMA_REG_CHUNK_SHIFT));
                    /* Check for particularly bad chunk value */
                    if (host_addr < (void *)block->local_host_addr) {
                        error_report("rdma: bad chunk for block %s"
                            " chunk: %" PRIx64,
                            block->block_name, reg->key.chunk);
                        goto err;
                    }
                }
                chunk_start = ram_chunk_start(block, chunk);
                chunk_end = ram_chunk_end(block, chunk + reg->chunks);
                /* avoid "-Waddress-of-packed-member" warning */
                uint32_t tmp_rkey = 0;
                if (qemu_rdma_register_and_get_keys(rdma, block,
                            (uintptr_t)host_addr, NULL, &tmp_rkey,
                            chunk, chunk_start, chunk_end)) {
                    error_report("cannot get rkey");
                    goto err;
                }
                reg_result->rkey = tmp_rkey;

                reg_result->host_addr = (uintptr_t)block->local_host_addr;

                trace_rdma_registration_handle_register_rkey(reg_result->rkey);

                result_to_network(reg_result);
            }

            ret = qemu_rdma_post_send_control(rdma,
                            (uint8_t *) results, &reg_resp, &err);

            if (ret < 0) {
                error_report_err(err);
                goto err;
            }
            break;
        case RDMA_CONTROL_UNREGISTER_REQUEST:
            trace_rdma_registration_handle_unregister(head.repeat);
            unreg_resp.repeat = head.repeat;
            registers = (RDMARegister *) rdma->wr_data[idx].control_curr;

            for (int count = 0; count < head.repeat; count++) {
                reg = &registers[count];
                network_to_register(reg);

                trace_rdma_registration_handle_unregister_loop(count,
                           reg->current_index, reg->key.chunk);

                block = &(rdma->local_ram_blocks.block[reg->current_index]);

                ret = ibv_dereg_mr(block->pmr[reg->key.chunk]);
                block->pmr[reg->key.chunk] = NULL;

                if (ret != 0) {
                    error_report("rdma unregistration chunk failed: %s",
                                 strerror(errno));
                    goto err;
                }

                rdma->total_registrations--;

                trace_rdma_registration_handle_unregister_success(reg->key.chunk);
            }

            ret = qemu_rdma_post_send_control(rdma, NULL, &unreg_resp, &err);

            if (ret < 0) {
                error_report_err(err);
                goto err;
            }
            break;
        case RDMA_CONTROL_REGISTER_RESULT:
            error_report("Invalid RESULT message at dest.");
            goto err;
        default:
            error_report("Unknown control message %s", control_desc(head.type));
            goto err;
        }
    } while (1);

err:
    rdma->errored = true;
    return -1;
}

/* Destination:
 * Called during the initial RAM load section which lists the
 * RAMBlocks by name.  This lets us know the order of the RAMBlocks on
 * the source.  We've already built our local RAMBlock list, but not
 * yet sent the list to the source.
 */
int rdma_block_notification_handle(QEMUFile *f, const char *name)
{
    int curr;
    int found = -1;

    if (!migrate_rdma()) {
        return 0;
    }

    RCU_READ_LOCK_GUARD();
    QIOChannelRDMA *rioc = QIO_CHANNEL_RDMA(qemu_file_get_ioc(f));
    RDMAContext *rdma = qatomic_rcu_read(&rioc->rdmain);

    if (!rdma) {
        return -1;
    }

    /* Find the matching RAMBlock in our local list */
    for (curr = 0; curr < rdma->local_ram_blocks.nb_blocks; curr++) {
        if (!strcmp(rdma->local_ram_blocks.block[curr].block_name, name)) {
            found = curr;
            break;
        }
    }

    if (found == -1) {
        error_report("RAMBlock '%s' not found on destination", name);
        return -1;
    }

    rdma->local_ram_blocks.block[curr].src_index = rdma->next_src_index;
    trace_rdma_block_notification_handle(name, rdma->next_src_index);
    rdma->next_src_index++;

    return 0;
}

int rdma_registration_start(QEMUFile *f, uint64_t flags)
{
    if (!migrate_rdma() || migration_in_postcopy()) {
        return 0;
    }

    QIOChannelRDMA *rioc = QIO_CHANNEL_RDMA(qemu_file_get_ioc(f));
    RCU_READ_LOCK_GUARD();
    RDMAContext *rdma = qatomic_rcu_read(&rioc->rdmaout);
    if (!rdma) {
        return -1;
    }

    if (rdma_errored(rdma)) {
        return -1;
    }

    trace_rdma_registration_start(flags);
    qemu_put_be64(f, RAM_SAVE_FLAG_HOOK);
    return qemu_fflush(f);
}

/*
 * Inform dest that dynamic registrations are done for now.
 * First, flush writes, if any.
 */
int rdma_registration_stop(QEMUFile *f, uint64_t flags)
{
    QIOChannelRDMA *rioc;
    Error *err = NULL;
    RDMAContext *rdma;
    RDMAControlHeader head = { .len = 0, .repeat = 1 };
    int ret;

    if (!migrate_rdma() || migration_in_postcopy()) {
        return 0;
    }

    RCU_READ_LOCK_GUARD();
    rioc = QIO_CHANNEL_RDMA(qemu_file_get_ioc(f));
    rdma = qatomic_rcu_read(&rioc->rdmaout);
    if (!rdma) {
        return -1;
    }

    if (rdma_errored(rdma)) {
        return -1;
    }

    qemu_fflush(f);
    ret = qemu_rdma_drain_cq(rdma);

    if (ret < 0) {
        goto err;
    }

    if (flags == RAM_CONTROL_SETUP) {
        RDMAControlHeader resp = {.type = RDMA_CONTROL_RAM_BLOCKS_RESULT };
        RDMALocalBlocks *local = &rdma->local_ram_blocks;
        int reg_result_idx, nb_dest_blocks;

        head.type = RDMA_CONTROL_RAM_BLOCKS_REQUEST;
        trace_rdma_registration_stop_ram();

        /*
         * Make sure that we parallelize the pinning on both sides.
         * For very large guests, doing this serially takes a really
         * long time, so we have to 'interleave' the pinning locally
         * with the control messages by performing the pinning on this
         * side before we receive the control response from the other
         * side that the pinning has completed.
         */
        ret = qemu_rdma_exchange_send(rdma, &head, NULL, &resp,
                    &reg_result_idx, rdma->pin_all ?
                    qemu_rdma_reg_whole_ram_blocks : NULL,
                    &err);
        if (ret < 0) {
            error_report_err(err);
            return -1;
        }

        nb_dest_blocks = resp.len / sizeof(RDMADestBlock);

        /*
         * The protocol uses two different sets of rkeys (mutually exclusive):
         * 1. One key to represent the virtual address of the entire ram block.
         *    (dynamic chunk registration disabled - pin everything with one rkey.)
         * 2. One to represent individual chunks within a ram block.
         *    (dynamic chunk registration enabled - pin individual chunks.)
         *
         * Once the capability is successfully negotiated, the destination transmits
         * the keys to use (or sends them later) including the virtual addresses
         * and then propagates the remote ram block descriptions to his local copy.
         */

        if (local->nb_blocks != nb_dest_blocks) {
            error_report("ram blocks mismatch (Number of blocks %d vs %d)",
                         local->nb_blocks, nb_dest_blocks);
            error_printf("Your QEMU command line parameters are probably "
                         "not identical on both the source and destination.");
            rdma->errored = true;
            return -1;
        }

        qemu_rdma_move_header(rdma, reg_result_idx, &resp);
        memcpy(rdma->dest_blocks,
            rdma->wr_data[reg_result_idx].control_curr, resp.len);
        for (int i = 0; i < nb_dest_blocks; i++) {
            network_to_dest_block(&rdma->dest_blocks[i]);

            /* We require that the blocks are in the same order */
            if (rdma->dest_blocks[i].length != local->block[i].length) {
                error_report("Block %s/%d has a different length %" PRIu64
                             "vs %" PRIu64,
                             local->block[i].block_name, i,
                             local->block[i].length,
                             rdma->dest_blocks[i].length);
                rdma->errored = true;
                return -1;
            }
            local->block[i].remote_host_addr =
                    rdma->dest_blocks[i].remote_host_addr;
            local->block[i].remote_rkey = rdma->dest_blocks[i].remote_rkey;
        }
    }

    trace_rdma_registration_stop(flags);

    head.type = RDMA_CONTROL_REGISTER_FINISHED;
    ret = qemu_rdma_exchange_send(rdma, &head, NULL, NULL, NULL, NULL, &err);

    if (ret < 0) {
        error_report_err(err);
        goto err;
    }

    return 0;
err:
    rdma->errored = true;
    return -1;
}

static void qio_channel_rdma_finalize(Object *obj)
{
    QIOChannelRDMA *rioc = QIO_CHANNEL_RDMA(obj);
    if (rioc->rdmain) {
        qemu_rdma_cleanup(rioc->rdmain);
        g_free(rioc->rdmain);
        rioc->rdmain = NULL;
    }
    if (rioc->rdmaout) {
        qemu_rdma_cleanup(rioc->rdmaout);
        g_free(rioc->rdmaout);
        rioc->rdmaout = NULL;
    }
}

static void qio_channel_rdma_class_init(ObjectClass *klass,
                                        void *class_data G_GNUC_UNUSED)
{
    QIOChannelClass *ioc_klass = QIO_CHANNEL_CLASS(klass);

    ioc_klass->io_writev = qio_channel_rdma_writev;
    ioc_klass->io_readv = qio_channel_rdma_readv;
    ioc_klass->io_set_blocking = qio_channel_rdma_set_blocking;
    ioc_klass->io_close = qio_channel_rdma_close;
    ioc_klass->io_create_watch = qio_channel_rdma_create_watch;
    ioc_klass->io_set_aio_fd_handler = qio_channel_rdma_set_aio_fd_handler;
    ioc_klass->io_shutdown = qio_channel_rdma_shutdown;
}

static const TypeInfo qio_channel_rdma_info = {
    .parent = TYPE_QIO_CHANNEL,
    .name = TYPE_QIO_CHANNEL_RDMA,
    .instance_size = sizeof(QIOChannelRDMA),
    .instance_finalize = qio_channel_rdma_finalize,
    .class_init = qio_channel_rdma_class_init,
};

static void qio_channel_rdma_register_types(void)
{
    type_register_static(&qio_channel_rdma_info);
}

type_init(qio_channel_rdma_register_types);

static QEMUFile *rdma_new_input(RDMAContext *rdma)
{
    QIOChannelRDMA *rioc = QIO_CHANNEL_RDMA(object_new(TYPE_QIO_CHANNEL_RDMA));

    rioc->file = qemu_file_new_input(QIO_CHANNEL(rioc));
    rioc->rdmain = rdma;
    rioc->rdmaout = rdma->return_path;

    return rioc->file;
}

static QEMUFile *rdma_new_output(RDMAContext *rdma)
{
    QIOChannelRDMA *rioc = QIO_CHANNEL_RDMA(object_new(TYPE_QIO_CHANNEL_RDMA));

    rioc->file = qemu_file_new_output(QIO_CHANNEL(rioc));
    rioc->rdmaout = rdma;
    rioc->rdmain = rdma->return_path;

    return rioc->file;
}

static void rdma_accept_incoming_migration(void *opaque)
{
    RDMAContext *rdma = opaque;
    QEMUFile *f;
    Error *local_err = NULL;

    trace_qemu_rdma_accept_incoming_migration();
    if (qemu_rdma_accept(rdma) < 0) {
        error_report("RDMA ERROR: Migration initialization failed");
        return;
    }

    trace_qemu_rdma_accept_incoming_migration_accepted();

    if (rdma->is_return_path) {
        return;
    }

    f = rdma_new_input(rdma);
    if (f == NULL) {
        error_report("RDMA ERROR: could not open RDMA for input");
        qemu_rdma_cleanup(rdma);
        return;
    }

    rdma->migration_started_on_destination = 1;
    migration_fd_process_incoming(f, &local_err);
    if (local_err) {
        error_reportf_err(local_err, "RDMA ERROR:");
    }
}

void rdma_start_incoming_migration(InetSocketAddress *host_port,
                                   Error **errp)
{
    MigrationState *s = migrate_get_current();
    int ret;
    RDMAContext *rdma;

    trace_rdma_start_incoming_migration();

    /* Avoid ram_block_discard_disable(), cannot change during migration. */
    if (ram_block_discard_is_required()) {
        error_setg(errp, "RDMA: cannot disable RAM discard");
        return;
    }

    rdma = qemu_rdma_data_init(host_port, errp);
    if (rdma == NULL) {
        goto err;
    }

    ret = qemu_rdma_dest_init(rdma, errp);
    if (ret < 0) {
        goto err;
    }

    trace_rdma_start_incoming_migration_after_dest_init();

    ret = rdma_listen(rdma->listen_id, 5);

    if (ret < 0) {
        error_setg(errp, "RDMA ERROR: listening on socket!");
        goto cleanup_rdma;
    }

    trace_rdma_start_incoming_migration_after_rdma_listen();
    s->rdma_migration = true;
    qemu_set_fd_handler(rdma->channel->fd, rdma_accept_incoming_migration,
                        NULL, (void *)(intptr_t)rdma);
    return;

cleanup_rdma:
    qemu_rdma_cleanup(rdma);
err:
    if (rdma) {
        g_free(rdma->host);
    }
    g_free(rdma);
}

void rdma_start_outgoing_migration(void *opaque,
                            InetSocketAddress *host_port, Error **errp)
{
    MigrationState *s = opaque;
    RDMAContext *rdma_return_path = NULL;
    RDMAContext *rdma;
    int ret;

    /* Avoid ram_block_discard_disable(), cannot change during migration. */
    if (ram_block_discard_is_required()) {
        error_setg(errp, "RDMA: cannot disable RAM discard");
        return;
    }

    rdma = qemu_rdma_data_init(host_port, errp);
    if (rdma == NULL) {
        goto err;
    }

    ret = qemu_rdma_source_init(rdma, migrate_rdma_pin_all(), errp);

    if (ret < 0) {
        goto err;
    }

    trace_rdma_start_outgoing_migration_after_rdma_source_init();
    ret = qemu_rdma_connect(rdma, false, errp);

    if (ret < 0) {
        goto err;
    }

    /* RDMA postcopy need a separate queue pair for return path */
    if (migrate_postcopy() || migrate_return_path()) {
        rdma_return_path = qemu_rdma_data_init(host_port, errp);

        if (rdma_return_path == NULL) {
            goto return_path_err;
        }

        ret = qemu_rdma_source_init(rdma_return_path,
                                    migrate_rdma_pin_all(), errp);

        if (ret < 0) {
            goto return_path_err;
        }

        ret = qemu_rdma_connect(rdma_return_path, true, errp);

        if (ret < 0) {
            goto return_path_err;
        }

        rdma->return_path = rdma_return_path;
        rdma_return_path->return_path = rdma;
        rdma_return_path->is_return_path = true;
    }

    trace_rdma_start_outgoing_migration_after_rdma_connect();

    s->to_dst_file = rdma_new_output(rdma);
    s->rdma_migration = true;
    migrate_fd_connect(s, NULL);
    return;
return_path_err:
    qemu_rdma_cleanup(rdma);
err:
    g_free(rdma);
    g_free(rdma_return_path);
}
