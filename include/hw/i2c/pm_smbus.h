#ifndef PM_SMBUS_H
#define PM_SMBUS_H

#include "hw/i2c/smbus_master.h"

#define PM_SMBUS_MAX_MSG_SIZE 32

typedef struct PMSMBus {
    I2CBus *smbus;
    MemoryRegion io;

    uint8_t smb_stat;
    uint8_t smb_ctl;
    uint8_t smb_cmd;
    uint8_t smb_addr;
    uint8_t smb_data0;
    uint8_t smb_data1;
    uint8_t smb_data[PM_SMBUS_MAX_MSG_SIZE];
    uint8_t smb_blkdata;
    uint8_t smb_auxctl;
    uint32_t smb_index;

    /* Set by pm_smbus.c */
    void (*reset)(struct PMSMBus *s);

    /* Set by the user. */
    bool i2c_enable;
    void (*set_irq)(struct PMSMBus *s, bool enabled);
    void *opaque;

    /* Internally used by pm_smbus. */

    /* Set on block transfers after the last byte has been read, so the
       INTR bit can be set at the right time. */
    bool op_done;

    /* Set during an I2C block read, so we know how to handle data. */
    bool in_i2c_block_read;

    /* Used to work around a bug in AMIBIOS, see smb_transaction_start() */
    bool start_transaction_on_status_read;
} PMSMBus;

void pm_smbus_init(DeviceState *parent, PMSMBus *smb, bool force_aux_blk);

/*
 * For backwards compatibility on migration, older versions don't have
 * working migration for pm_smbus, this lets us ignore the migrations
 * for older machine versions.
 */
bool pm_smbus_vmstate_needed(void);

extern const VMStateDescription pmsmb_vmstate;

#endif /* PM_SMBUS_H */
