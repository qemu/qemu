/*
 * QEMU CXL Devices
 *
 * Copyright (c) 2020 Intel
 *
 * This work is licensed under the terms of the GNU GPL, version 2. See the
 * COPYING file in the top-level directory.
 */

#ifndef CXL_DEVICE_H
#define CXL_DEVICE_H

#include "hw/cxl/cxl_component.h"
#include "hw/pci/pci_device.h"
#include "hw/register.h"
#include "hw/cxl/cxl_events.h"

/*
 * The following is how a CXL device's Memory Device registers are laid out.
 * The only requirement from the spec is that the capabilities array and the
 * capability headers start at offset 0 and are contiguously packed. The headers
 * themselves provide offsets to the register fields. For this emulation, the
 * actual registers  * will start at offset 0x80 (m == 0x80). No secondary
 * mailbox is implemented which means that the offset of the start of the
 * mailbox payload (n) is given by
 * n = m + sizeof(mailbox registers) + sizeof(device registers).
 *
 *                       +---------------------------------+
 *                       |                                 |
 *                       |    Memory Device Registers      |
 *                       |                                 |
 * n + PAYLOAD_SIZE_MAX  -----------------------------------
 *                  ^    |                                 |
 *                  |    |                                 |
 *                  |    |                                 |
 *                  |    |                                 |
 *                  |    |                                 |
 *                  |    |         Mailbox Payload         |
 *                  |    |                                 |
 *                  |    |                                 |
 *                  |    |                                 |
 *                  n    -----------------------------------
 *                  ^    |       Mailbox Registers         |
 *                  |    |                                 |
 *                  |    -----------------------------------
 *                  |    |                                 |
 *                  |    |        Device Registers         |
 *                  |    |                                 |
 *                  m    ---------------------------------->
 *                  ^    |  Memory Device Capability Header|
 *                  |    -----------------------------------
 *                  |    |     Mailbox Capability Header   |
 *                  |    -----------------------------------
 *                  |    |     Device Capability Header    |
 *                  |    -----------------------------------
 *                  |    |     Device Cap Array Register   |
 *                  0    +---------------------------------+
 *
 */

#define CXL_DEVICE_CAP_HDR1_OFFSET 0x10 /* Figure 138 */
#define CXL_DEVICE_CAP_REG_SIZE 0x10 /* 8.2.8.2 */
#define CXL_DEVICE_CAPS_MAX 4 /* 8.2.8.2.1 + 8.2.8.5 */
#define CXL_CAPS_SIZE \
    (CXL_DEVICE_CAP_REG_SIZE * (CXL_DEVICE_CAPS_MAX + 1)) /* +1 for header */

#define CXL_DEVICE_STATUS_REGISTERS_OFFSET 0x80 /* Read comment above */
#define CXL_DEVICE_STATUS_REGISTERS_LENGTH 0x8 /* 8.2.8.3.1 */

#define CXL_MAILBOX_REGISTERS_OFFSET \
    (CXL_DEVICE_STATUS_REGISTERS_OFFSET + CXL_DEVICE_STATUS_REGISTERS_LENGTH)
#define CXL_MAILBOX_REGISTERS_SIZE 0x20 /* 8.2.8.4, Figure 139 */
#define CXL_MAILBOX_PAYLOAD_SHIFT 11
#define CXL_MAILBOX_MAX_PAYLOAD_SIZE (1 << CXL_MAILBOX_PAYLOAD_SHIFT)
#define CXL_MAILBOX_REGISTERS_LENGTH \
    (CXL_MAILBOX_REGISTERS_SIZE + CXL_MAILBOX_MAX_PAYLOAD_SIZE)

#define CXL_MEMORY_DEVICE_REGISTERS_OFFSET \
    (CXL_MAILBOX_REGISTERS_OFFSET + CXL_MAILBOX_REGISTERS_LENGTH)
#define CXL_MEMORY_DEVICE_REGISTERS_LENGTH 0x8

#define CXL_MMIO_SIZE                                                   \
    (CXL_DEVICE_CAP_REG_SIZE + CXL_DEVICE_STATUS_REGISTERS_LENGTH +     \
     CXL_MAILBOX_REGISTERS_LENGTH + CXL_MEMORY_DEVICE_REGISTERS_LENGTH)

/* 8.2.8.4.5.1 Command Return Codes */
typedef enum {
    CXL_MBOX_SUCCESS = 0x0,
    CXL_MBOX_BG_STARTED = 0x1,
    CXL_MBOX_INVALID_INPUT = 0x2,
    CXL_MBOX_UNSUPPORTED = 0x3,
    CXL_MBOX_INTERNAL_ERROR = 0x4,
    CXL_MBOX_RETRY_REQUIRED = 0x5,
    CXL_MBOX_BUSY = 0x6,
    CXL_MBOX_MEDIA_DISABLED = 0x7,
    CXL_MBOX_FW_XFER_IN_PROGRESS = 0x8,
    CXL_MBOX_FW_XFER_OUT_OF_ORDER = 0x9,
    CXL_MBOX_FW_AUTH_FAILED = 0xa,
    CXL_MBOX_FW_INVALID_SLOT = 0xb,
    CXL_MBOX_FW_ROLLEDBACK = 0xc,
    CXL_MBOX_FW_REST_REQD = 0xd,
    CXL_MBOX_INVALID_HANDLE = 0xe,
    CXL_MBOX_INVALID_PA = 0xf,
    CXL_MBOX_INJECT_POISON_LIMIT = 0x10,
    CXL_MBOX_PERMANENT_MEDIA_FAILURE = 0x11,
    CXL_MBOX_ABORTED = 0x12,
    CXL_MBOX_INVALID_SECURITY_STATE = 0x13,
    CXL_MBOX_INCORRECT_PASSPHRASE = 0x14,
    CXL_MBOX_UNSUPPORTED_MAILBOX = 0x15,
    CXL_MBOX_INVALID_PAYLOAD_LENGTH = 0x16,
    CXL_MBOX_MAX = 0x17
} CXLRetCode;

typedef struct CXLCCI CXLCCI;
typedef struct cxl_device_state CXLDeviceState;
struct cxl_cmd;
typedef CXLRetCode (*opcode_handler)(const struct cxl_cmd *cmd,
                                     uint8_t *payload_in, size_t len_in,
                                     uint8_t *payload_out, size_t *len_out,
                                     CXLCCI *cci);
struct cxl_cmd {
    const char *name;
    opcode_handler handler;
    ssize_t in;
    uint16_t effect; /* Reported in CEL */
};

typedef struct CXLEvent {
    CXLEventRecordRaw data;
    QSIMPLEQ_ENTRY(CXLEvent) node;
} CXLEvent;

typedef struct CXLEventLog {
    uint16_t next_handle;
    uint16_t overflow_err_count;
    uint64_t first_overflow_timestamp;
    uint64_t last_overflow_timestamp;
    bool irq_enabled;
    int irq_vec;
    QemuMutex lock;
    QSIMPLEQ_HEAD(, CXLEvent) events;
} CXLEventLog;

typedef struct CXLCCI {
    const struct cxl_cmd (*cxl_cmd_set)[256];
    struct cel_log {
        uint16_t opcode;
        uint16_t effect;
    } cel_log[1 << 16];
    size_t cel_size;

    /* background command handling (times in ms) */
    struct {
        uint16_t opcode;
        uint16_t complete_pct;
        uint16_t ret_code; /* Current value of retcode */
        uint64_t starttime;
        /* set by each bg cmd, cleared by the bg_timer when complete */
        uint64_t runtime;
        QEMUTimer *timer;
    } bg;
    size_t payload_max;
    /* Pointer to device hosting the CCI */
    DeviceState *d;
    /* Pointer to the device hosting the protocol conversion */
    DeviceState *intf;
} CXLCCI;

typedef struct cxl_device_state {
    MemoryRegion device_registers;

    /* mmio for device capabilities array - 8.2.8.2 */
    struct {
        MemoryRegion device;
        union {
            uint8_t dev_reg_state[CXL_DEVICE_STATUS_REGISTERS_LENGTH];
            uint16_t dev_reg_state16[CXL_DEVICE_STATUS_REGISTERS_LENGTH / 2];
            uint32_t dev_reg_state32[CXL_DEVICE_STATUS_REGISTERS_LENGTH / 4];
            uint64_t dev_reg_state64[CXL_DEVICE_STATUS_REGISTERS_LENGTH / 8];
        };
        uint64_t event_status;
    };
    MemoryRegion memory_device;
    struct {
        MemoryRegion caps;
        union {
            uint32_t caps_reg_state32[CXL_CAPS_SIZE / 4];
            uint64_t caps_reg_state64[CXL_CAPS_SIZE / 8];
        };
    };

    /* mmio for the mailbox registers 8.2.8.4 */
    struct {
        MemoryRegion mailbox;
        uint16_t payload_size;
        uint8_t mbox_msi_n;
        union {
            uint8_t mbox_reg_state[CXL_MAILBOX_REGISTERS_LENGTH];
            uint16_t mbox_reg_state16[CXL_MAILBOX_REGISTERS_LENGTH / 2];
            uint32_t mbox_reg_state32[CXL_MAILBOX_REGISTERS_LENGTH / 4];
            uint64_t mbox_reg_state64[CXL_MAILBOX_REGISTERS_LENGTH / 8];
        };
    };

    struct {
        bool set;
        uint64_t last_set;
        uint64_t host_set;
    } timestamp;

    /* memory region size, HDM */
    uint64_t mem_size;
    uint64_t pmem_size;
    uint64_t vmem_size;

    const struct cxl_cmd (*cxl_cmd_set)[256];
    CXLEventLog event_logs[CXL_EVENT_TYPE_MAX];
} CXLDeviceState;

/* Initialize the register block for a device */
void cxl_device_register_block_init(Object *obj, CXLDeviceState *dev,
                                    CXLCCI *cci);

typedef struct CXLType3Dev CXLType3Dev;
typedef struct CSWMBCCIDev CSWMBCCIDev;
/* Set up default values for the register block */
void cxl_device_register_init_t3(CXLType3Dev *ct3d);
void cxl_device_register_init_swcci(CSWMBCCIDev *sw);

/*
 * CXL 2.0 - 8.2.8.1 including errata F4
 * Documented as a 128 bit register, but 64 bit accesses and the second
 * 64 bits are currently reserved.
 */
REG64(CXL_DEV_CAP_ARRAY, 0)
    FIELD(CXL_DEV_CAP_ARRAY, CAP_ID, 0, 16)
    FIELD(CXL_DEV_CAP_ARRAY, CAP_VERSION, 16, 8)
    FIELD(CXL_DEV_CAP_ARRAY, CAP_COUNT, 32, 16)

void cxl_event_set_status(CXLDeviceState *cxl_dstate, CXLEventLogType log_type,
                          bool available);

/*
 * Helper macro to initialize capability headers for CXL devices.
 *
 * In the 8.2.8.2, this is listed as a 128b register, but in 8.2.8, it says:
 * > No registers defined in Section 8.2.8 are larger than 64-bits wide so that
 * > is the maximum access size allowed for these registers. If this rule is not
 * > followed, the behavior is undefined
 *
 * CXL 2.0 Errata F4 states further that the layouts in the specification are
 * shown as greater than 128 bits, but implementations are expected to
 * use any size of access up to 64 bits.
 *
 * Here we've chosen to make it 4 dwords. The spec allows any pow2 multiple
 * access to be used for a register up to 64 bits.
 */
#define CXL_DEVICE_CAPABILITY_HEADER_REGISTER(n, offset)  \
    REG32(CXL_DEV_##n##_CAP_HDR0, offset)                 \
        FIELD(CXL_DEV_##n##_CAP_HDR0, CAP_ID, 0, 16)      \
        FIELD(CXL_DEV_##n##_CAP_HDR0, CAP_VERSION, 16, 8) \
    REG32(CXL_DEV_##n##_CAP_HDR1, offset + 4)             \
        FIELD(CXL_DEV_##n##_CAP_HDR1, CAP_OFFSET, 0, 32)  \
    REG32(CXL_DEV_##n##_CAP_HDR2, offset + 8)             \
        FIELD(CXL_DEV_##n##_CAP_HDR2, CAP_LENGTH, 0, 32)

CXL_DEVICE_CAPABILITY_HEADER_REGISTER(DEVICE_STATUS, CXL_DEVICE_CAP_HDR1_OFFSET)
CXL_DEVICE_CAPABILITY_HEADER_REGISTER(MAILBOX, CXL_DEVICE_CAP_HDR1_OFFSET + \
                                               CXL_DEVICE_CAP_REG_SIZE)
CXL_DEVICE_CAPABILITY_HEADER_REGISTER(MEMORY_DEVICE,
                                      CXL_DEVICE_CAP_HDR1_OFFSET +
                                          CXL_DEVICE_CAP_REG_SIZE * 2)

void cxl_initialize_mailbox_t3(CXLCCI *cci, DeviceState *d, size_t payload_max);
void cxl_initialize_mailbox_swcci(CXLCCI *cci, DeviceState *intf,
                                  DeviceState *d, size_t payload_max);
void cxl_init_cci(CXLCCI *cci, size_t payload_max);
int cxl_process_cci_message(CXLCCI *cci, uint8_t set, uint8_t cmd,
                            size_t len_in, uint8_t *pl_in,
                            size_t *len_out, uint8_t *pl_out,
                            bool *bg_started);
void cxl_initialize_t3_fm_owned_ld_mctpcci(CXLCCI *cci, DeviceState *d,
                                           DeviceState *intf,
                                           size_t payload_max);

void cxl_initialize_t3_ld_cci(CXLCCI *cci, DeviceState *d,
                              DeviceState *intf, size_t payload_max);

#define cxl_device_cap_init(dstate, reg, cap_id, ver)                      \
    do {                                                                   \
        uint32_t *cap_hdrs = dstate->caps_reg_state32;                     \
        int which = R_CXL_DEV_##reg##_CAP_HDR0;                            \
        cap_hdrs[which] =                                                  \
            FIELD_DP32(cap_hdrs[which], CXL_DEV_##reg##_CAP_HDR0,          \
                       CAP_ID, cap_id);                                    \
        cap_hdrs[which] = FIELD_DP32(                                      \
            cap_hdrs[which], CXL_DEV_##reg##_CAP_HDR0, CAP_VERSION, ver);  \
        cap_hdrs[which + 1] =                                              \
            FIELD_DP32(cap_hdrs[which + 1], CXL_DEV_##reg##_CAP_HDR1,      \
                       CAP_OFFSET, CXL_##reg##_REGISTERS_OFFSET);          \
        cap_hdrs[which + 2] =                                              \
            FIELD_DP32(cap_hdrs[which + 2], CXL_DEV_##reg##_CAP_HDR2,      \
                       CAP_LENGTH, CXL_##reg##_REGISTERS_LENGTH);          \
    } while (0)

/* CXL 3.0 8.2.8.3.1 Event Status Register */
REG64(CXL_DEV_EVENT_STATUS, 0)
    FIELD(CXL_DEV_EVENT_STATUS, EVENT_STATUS, 0, 32)

/* CXL 2.0 8.2.8.4.3 Mailbox Capabilities Register */
REG32(CXL_DEV_MAILBOX_CAP, 0)
    FIELD(CXL_DEV_MAILBOX_CAP, PAYLOAD_SIZE, 0, 5)
    FIELD(CXL_DEV_MAILBOX_CAP, INT_CAP, 5, 1)
    FIELD(CXL_DEV_MAILBOX_CAP, BG_INT_CAP, 6, 1)
    FIELD(CXL_DEV_MAILBOX_CAP, MSI_N, 7, 4)

/* CXL 2.0 8.2.8.4.4 Mailbox Control Register */
REG32(CXL_DEV_MAILBOX_CTRL, 4)
    FIELD(CXL_DEV_MAILBOX_CTRL, DOORBELL, 0, 1)
    FIELD(CXL_DEV_MAILBOX_CTRL, INT_EN, 1, 1)
    FIELD(CXL_DEV_MAILBOX_CTRL, BG_INT_EN, 2, 1)

/* CXL 2.0 8.2.8.4.5 Command Register */
REG64(CXL_DEV_MAILBOX_CMD, 8)
    FIELD(CXL_DEV_MAILBOX_CMD, COMMAND, 0, 8)
    FIELD(CXL_DEV_MAILBOX_CMD, COMMAND_SET, 8, 8)
    FIELD(CXL_DEV_MAILBOX_CMD, LENGTH, 16, 20)

/* CXL 2.0 8.2.8.4.6 Mailbox Status Register */
REG64(CXL_DEV_MAILBOX_STS, 0x10)
    FIELD(CXL_DEV_MAILBOX_STS, BG_OP, 0, 1)
    FIELD(CXL_DEV_MAILBOX_STS, ERRNO, 32, 16)
    FIELD(CXL_DEV_MAILBOX_STS, VENDOR_ERRNO, 48, 16)

/* CXL 2.0 8.2.8.4.7 Background Command Status Register */
REG64(CXL_DEV_BG_CMD_STS, 0x18)
    FIELD(CXL_DEV_BG_CMD_STS, OP, 0, 16)
    FIELD(CXL_DEV_BG_CMD_STS, PERCENTAGE_COMP, 16, 7)
    FIELD(CXL_DEV_BG_CMD_STS, RET_CODE, 32, 16)
    FIELD(CXL_DEV_BG_CMD_STS, VENDOR_RET_CODE, 48, 16)

/* CXL 2.0 8.2.8.4.8 Command Payload Registers */
REG32(CXL_DEV_CMD_PAYLOAD, 0x20)

REG64(CXL_MEM_DEV_STS, 0)
    FIELD(CXL_MEM_DEV_STS, FATAL, 0, 1)
    FIELD(CXL_MEM_DEV_STS, FW_HALT, 1, 1)
    FIELD(CXL_MEM_DEV_STS, MEDIA_STATUS, 2, 2)
    FIELD(CXL_MEM_DEV_STS, MBOX_READY, 4, 1)
    FIELD(CXL_MEM_DEV_STS, RESET_NEEDED, 5, 3)

static inline void __toggle_media(CXLDeviceState *cxl_dstate, int val)
{
    uint64_t dev_status_reg;

    dev_status_reg = FIELD_DP64(0, CXL_MEM_DEV_STS, MEDIA_STATUS, val);
    cxl_dstate->mbox_reg_state64[R_CXL_MEM_DEV_STS] = dev_status_reg;
}
#define cxl_dev_disable_media(cxlds)                    \
        do { __toggle_media((cxlds), 0x3); } while (0)
#define cxl_dev_enable_media(cxlds)                     \
        do { __toggle_media((cxlds), 0x1); } while (0)

static inline bool sanitize_running(CXLCCI *cci)
{
    return !!cci->bg.runtime && cci->bg.opcode == 0x4400;
}

typedef struct CXLError {
    QTAILQ_ENTRY(CXLError) node;
    int type; /* Error code as per FE definition */
    uint32_t header[CXL_RAS_ERR_HEADER_NUM];
} CXLError;

typedef QTAILQ_HEAD(, CXLError) CXLErrorList;

typedef struct CXLPoison {
    uint64_t start, length;
    uint8_t type;
#define CXL_POISON_TYPE_EXTERNAL 0x1
#define CXL_POISON_TYPE_INTERNAL 0x2
#define CXL_POISON_TYPE_INJECTED 0x3
    QLIST_ENTRY(CXLPoison) node;
} CXLPoison;

typedef QLIST_HEAD(, CXLPoison) CXLPoisonList;
#define CXL_POISON_LIST_LIMIT 256

struct CXLType3Dev {
    /* Private */
    PCIDevice parent_obj;

    /* Properties */
    HostMemoryBackend *hostmem; /* deprecated */
    HostMemoryBackend *hostvmem;
    HostMemoryBackend *hostpmem;
    HostMemoryBackend *lsa;
    uint64_t sn;

    /* State */
    AddressSpace hostvmem_as;
    AddressSpace hostpmem_as;
    CXLComponentState cxl_cstate;
    CXLDeviceState cxl_dstate;
    CXLCCI cci; /* Primary PCI mailbox CCI */
    /* Always initialized as no way to know if a VDM might show up */
    CXLCCI vdm_fm_owned_ld_mctp_cci;
    CXLCCI ld0_cci;

    /* DOE */
    DOECap doe_cdat;

    /* Error injection */
    CXLErrorList error_list;

    /* Poison Injection - cache */
    CXLPoisonList poison_list;
    unsigned int poison_list_cnt;
    bool poison_list_overflowed;
    uint64_t poison_list_overflow_ts;
};

#define TYPE_CXL_TYPE3 "cxl-type3"
OBJECT_DECLARE_TYPE(CXLType3Dev, CXLType3Class, CXL_TYPE3)

struct CXLType3Class {
    /* Private */
    PCIDeviceClass parent_class;

    /* public */
    uint64_t (*get_lsa_size)(CXLType3Dev *ct3d);

    uint64_t (*get_lsa)(CXLType3Dev *ct3d, void *buf, uint64_t size,
                        uint64_t offset);
    void (*set_lsa)(CXLType3Dev *ct3d, const void *buf, uint64_t size,
                    uint64_t offset);
    bool (*set_cacheline)(CXLType3Dev *ct3d, uint64_t dpa_offset,
                          uint8_t *data);
};

struct CSWMBCCIDev {
    PCIDevice parent_obj;
    PCIDevice *target;
    CXLComponentState cxl_cstate;
    CXLDeviceState cxl_dstate;
    CXLCCI *cci;
};

#define TYPE_CXL_SWITCH_MAILBOX_CCI "cxl-switch-mailbox-cci"
OBJECT_DECLARE_TYPE(CSWMBCCIDev, CSWMBCCIClass, CXL_SWITCH_MAILBOX_CCI)

MemTxResult cxl_type3_read(PCIDevice *d, hwaddr host_addr, uint64_t *data,
                           unsigned size, MemTxAttrs attrs);
MemTxResult cxl_type3_write(PCIDevice *d, hwaddr host_addr, uint64_t data,
                            unsigned size, MemTxAttrs attrs);

uint64_t cxl_device_get_timestamp(CXLDeviceState *cxlds);

void cxl_event_init(CXLDeviceState *cxlds, int start_msg_num);
bool cxl_event_insert(CXLDeviceState *cxlds, CXLEventLogType log_type,
                      CXLEventRecordRaw *event);
CXLRetCode cxl_event_get_records(CXLDeviceState *cxlds, CXLGetEventPayload *pl,
                                 uint8_t log_type, int max_recs,
                                 size_t *len);
CXLRetCode cxl_event_clear_records(CXLDeviceState *cxlds,
                                   CXLClearEventPayload *pl);

void cxl_event_irq_assert(CXLType3Dev *ct3d);

void cxl_set_poison_list_overflowed(CXLType3Dev *ct3d);

#endif
