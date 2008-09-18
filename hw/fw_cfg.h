#ifndef FW_CFG_H
#define FW_CFG_H

#define FW_CFG_SIGNATURE        0x00
#define FW_CFG_ID               0x01
#define FW_CFG_UUID             0x02
#define FW_CFG_RAM_SIZE         0x03
#define FW_CFG_NOGRAPHIC        0x04
#define FW_CFG_NB_CPUS          0x05
#define FW_CFG_MACHINE_ID       0x06
#define FW_CFG_MAX_ENTRY        0x10

#define FW_CFG_WRITE_CHANNEL    0x4000
#define FW_CFG_ARCH_LOCAL       0x8000
#define FW_CFG_ENTRY_MASK       ~(FW_CFG_WRITE_CHANNEL | FW_CFG_ARCH_LOCAL)

#define FW_CFG_INVALID          0xffff

#ifndef NO_QEMU_PROTOS
typedef void (*FWCfgCallback)(void *opaque, uint8_t *data);

int fw_cfg_add_bytes(void *opaque, uint16_t key, uint8_t *data, uint16_t len);
int fw_cfg_add_i16(void *opaque, uint16_t key, uint16_t value);
int fw_cfg_add_i32(void *opaque, uint16_t key, uint32_t value);
int fw_cfg_add_i64(void *opaque, uint16_t key, uint64_t value);
int fw_cfg_add_callback(void *opaque, uint16_t key, FWCfgCallback callback,
                        void *callback_opaque, uint8_t *data, size_t len);
void *fw_cfg_init(uint32_t ctl_port, uint32_t data_port,
		target_phys_addr_t crl_addr, target_phys_addr_t data_addr);

#endif /* NO_QEMU_PROTOS */

#endif
