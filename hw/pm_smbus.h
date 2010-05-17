#ifndef PM_SMBUS_H
#define PM_SMBUS_H

typedef struct PMSMBus {
    i2c_bus *smbus;

    uint8_t smb_stat;
    uint8_t smb_ctl;
    uint8_t smb_cmd;
    uint8_t smb_addr;
    uint8_t smb_data0;
    uint8_t smb_data1;
    uint8_t smb_data[32];
    uint8_t smb_index;
} PMSMBus;

void pm_smbus_init(DeviceState *parent, PMSMBus *smb);
void smb_ioport_writeb(void *opaque, uint32_t addr, uint32_t val);
uint32_t smb_ioport_readb(void *opaque, uint32_t addr);

#endif /* !PM_SMBUS_H */
