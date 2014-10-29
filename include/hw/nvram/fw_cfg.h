#ifndef FW_CFG_H
#define FW_CFG_H

#ifndef NO_QEMU_PROTOS
#include <stdint.h>
#include <stddef.h>

#include "exec/hwaddr.h"
#include "qemu/typedefs.h"
#endif

#define FW_CFG_SIGNATURE        0x00
#define FW_CFG_ID               0x01
#define FW_CFG_UUID             0x02
#define FW_CFG_RAM_SIZE         0x03
#define FW_CFG_NOGRAPHIC        0x04
#define FW_CFG_NB_CPUS          0x05
#define FW_CFG_MACHINE_ID       0x06
#define FW_CFG_KERNEL_ADDR      0x07
#define FW_CFG_KERNEL_SIZE      0x08
#define FW_CFG_KERNEL_CMDLINE   0x09
#define FW_CFG_INITRD_ADDR      0x0a
#define FW_CFG_INITRD_SIZE      0x0b
#define FW_CFG_BOOT_DEVICE      0x0c
#define FW_CFG_NUMA             0x0d
#define FW_CFG_BOOT_MENU        0x0e
#define FW_CFG_MAX_CPUS         0x0f
#define FW_CFG_KERNEL_ENTRY     0x10
#define FW_CFG_KERNEL_DATA      0x11
#define FW_CFG_INITRD_DATA      0x12
#define FW_CFG_CMDLINE_ADDR     0x13
#define FW_CFG_CMDLINE_SIZE     0x14
#define FW_CFG_CMDLINE_DATA     0x15
#define FW_CFG_SETUP_ADDR       0x16
#define FW_CFG_SETUP_SIZE       0x17
#define FW_CFG_SETUP_DATA       0x18
#define FW_CFG_FILE_DIR         0x19

#define FW_CFG_FILE_FIRST       0x20
#define FW_CFG_FILE_SLOTS       0x10
#define FW_CFG_MAX_ENTRY        (FW_CFG_FILE_FIRST+FW_CFG_FILE_SLOTS)

#define FW_CFG_WRITE_CHANNEL    0x4000
#define FW_CFG_ARCH_LOCAL       0x8000
#define FW_CFG_ENTRY_MASK       ~(FW_CFG_WRITE_CHANNEL | FW_CFG_ARCH_LOCAL)

#define FW_CFG_INVALID          0xffff

#define FW_CFG_MAX_FILE_PATH    56

#ifndef NO_QEMU_PROTOS
typedef struct FWCfgFile {
    uint32_t  size;        /* file size */
    uint16_t  select;      /* write this to 0x510 to read it */
    uint16_t  reserved;
    char      name[FW_CFG_MAX_FILE_PATH];
} FWCfgFile;

typedef struct FWCfgFiles {
    uint32_t  count;
    FWCfgFile f[];
} FWCfgFiles;

typedef void (*FWCfgCallback)(void *opaque, uint8_t *data);
typedef void (*FWCfgReadCallback)(void *opaque, uint32_t offset);

void fw_cfg_add_bytes(FWCfgState *s, uint16_t key, void *data, size_t len);
void fw_cfg_add_string(FWCfgState *s, uint16_t key, const char *value);
void fw_cfg_add_i16(FWCfgState *s, uint16_t key, uint16_t value);
void fw_cfg_add_i32(FWCfgState *s, uint16_t key, uint32_t value);
void fw_cfg_add_i64(FWCfgState *s, uint16_t key, uint64_t value);
void fw_cfg_add_callback(FWCfgState *s, uint16_t key, FWCfgCallback callback,
                         void *callback_opaque, void *data, size_t len);
void fw_cfg_add_file(FWCfgState *s, const char *filename, void *data,
                     size_t len);
void fw_cfg_add_file_callback(FWCfgState *s, const char *filename,
                              FWCfgReadCallback callback, void *callback_opaque,
                              void *data, size_t len);
void *fw_cfg_modify_file(FWCfgState *s, const char *filename, void *data,
                         size_t len);
FWCfgState *fw_cfg_init(uint32_t ctl_port, uint32_t data_port,
                        hwaddr crl_addr, hwaddr data_addr);

FWCfgState *fw_cfg_find(void);

#endif /* NO_QEMU_PROTOS */

#endif
