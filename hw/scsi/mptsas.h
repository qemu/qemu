#ifndef MPTSAS_H
#define MPTSAS_H

#include "mpi.h"

#define MPTSAS_NUM_PORTS 8
#define MPTSAS_MAX_FRAMES 2048     /* Firmware limit at 65535 */

#define MPTSAS_REQUEST_QUEUE_DEPTH 128
#define MPTSAS_REPLY_QUEUE_DEPTH   128

#define MPTSAS_MAXIMUM_CHAIN_DEPTH 0x22

typedef struct MPTSASState MPTSASState;
typedef struct MPTSASRequest MPTSASRequest;

enum {
    DOORBELL_NONE,
    DOORBELL_WRITE,
    DOORBELL_READ
};

struct MPTSASState {
    PCIDevice dev;
    MemoryRegion mmio_io;
    MemoryRegion port_io;
    MemoryRegion diag_io;
    QEMUBH *request_bh;

    /* properties */
    OnOffAuto msi;
    uint64_t sas_addr;

    bool msi_in_use;

    /* Doorbell register */
    uint32_t state;
    uint8_t who_init;
    uint8_t doorbell_state;

    /* Buffer for requests that are sent through the doorbell register.  */
    uint32_t doorbell_msg[256];
    int doorbell_idx;
    int doorbell_cnt;

    uint16_t doorbell_reply[256];
    int doorbell_reply_idx;
    int doorbell_reply_size;

    /* Other registers */
    uint8_t diagnostic_idx;
    uint32_t diagnostic;
    uint32_t intr_mask;
    uint32_t intr_status;

    /* Request queues */
    uint32_t request_post[MPTSAS_REQUEST_QUEUE_DEPTH + 1];
    uint16_t request_post_head;
    uint16_t request_post_tail;

    uint32_t reply_post[MPTSAS_REPLY_QUEUE_DEPTH + 1];
    uint16_t reply_post_head;
    uint16_t reply_post_tail;

    uint32_t reply_free[MPTSAS_REPLY_QUEUE_DEPTH + 1];
    uint16_t reply_free_head;
    uint16_t reply_free_tail;

    /* IOC Facts */
    hwaddr host_mfa_high_addr;
    hwaddr sense_buffer_high_addr;
    uint16_t max_devices;
    uint16_t max_buses;
    uint16_t reply_frame_size;

    SCSIBus bus;
    QTAILQ_HEAD(, MPTSASRequest) pending;
};

void mptsas_fix_scsi_io_endianness(MPIMsgSCSIIORequest *req);
void mptsas_fix_scsi_io_reply_endianness(MPIMsgSCSIIOReply *reply);
void mptsas_fix_scsi_task_mgmt_endianness(MPIMsgSCSITaskMgmt *req);
void mptsas_fix_scsi_task_mgmt_reply_endianness(MPIMsgSCSITaskMgmtReply *reply);
void mptsas_fix_ioc_init_endianness(MPIMsgIOCInit *req);
void mptsas_fix_ioc_init_reply_endianness(MPIMsgIOCInitReply *reply);
void mptsas_fix_ioc_facts_endianness(MPIMsgIOCFacts *req);
void mptsas_fix_ioc_facts_reply_endianness(MPIMsgIOCFactsReply *reply);
void mptsas_fix_config_endianness(MPIMsgConfig *req);
void mptsas_fix_config_reply_endianness(MPIMsgConfigReply *reply);
void mptsas_fix_port_facts_endianness(MPIMsgPortFacts *req);
void mptsas_fix_port_facts_reply_endianness(MPIMsgPortFactsReply *reply);
void mptsas_fix_port_enable_endianness(MPIMsgPortEnable *req);
void mptsas_fix_port_enable_reply_endianness(MPIMsgPortEnableReply *reply);
void mptsas_fix_event_notification_endianness(MPIMsgEventNotify *req);
void mptsas_fix_event_notification_reply_endianness(MPIMsgEventNotifyReply *reply);

void mptsas_reply(MPTSASState *s, MPIDefaultReply *reply);

void mptsas_process_config(MPTSASState *s, MPIMsgConfig *req);

#endif /* MPTSAS_H */
