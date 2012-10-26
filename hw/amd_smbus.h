#ifndef AMD_SMBUS_H
#define AMD_SMBUS_H

typedef struct AMD756SMBus {
    i2c_bus *smbus;

    uint8_t smb_stat;
    uint8_t smb_ctl;
    uint8_t smb_cmd;
    uint8_t smb_addr;
    uint8_t smb_data0;
    uint8_t smb_data1;
    uint8_t smb_data[32];
    uint8_t smb_index;

    qemu_irq irq;
} AMD756SMBus;

void amd756_smbus_init(DeviceState *parent, AMD756SMBus *smb, qemu_irq irq);
void amd756_smb_ioport_writeb(void *opaque, uint32_t addr, uint32_t val);
uint32_t amd756_smb_ioport_readb(void *opaque, uint32_t addr);

#endif /* !AMD_SMBUS_H */
