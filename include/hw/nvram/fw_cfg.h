#ifndef FW_CFG_H
#define FW_CFG_H

#ifndef NO_QEMU_PROTOS

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

/* width in bytes of fw_cfg control register */
#define FW_CFG_CTL_SIZE         0x02

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

/* Control as first field allows for different structures selected by this
 * field, which might be useful in the future
 */
typedef struct FWCfgDmaAccess {
    uint32_t control;
    uint32_t length;
    uint64_t address;
} QEMU_PACKED FWCfgDmaAccess;

typedef void (*FWCfgReadCallback)(void *opaque);

/**
 * fw_cfg_add_bytes:
 * @s: fw_cfg device being modified
 * @key: selector key value for new fw_cfg item
 * @data: pointer to start of item data
 * @len: size of item data
 *
 * Add a new fw_cfg item, available by selecting the given key, as a raw
 * "blob" of the given size. The data referenced by the starting pointer
 * is only linked, NOT copied, into the data structure of the fw_cfg device.
 */
void fw_cfg_add_bytes(FWCfgState *s, uint16_t key, void *data, size_t len);

/**
 * fw_cfg_add_string:
 * @s: fw_cfg device being modified
 * @key: selector key value for new fw_cfg item
 * @value: NUL-terminated ascii string
 *
 * Add a new fw_cfg item, available by selecting the given key. The item
 * data will consist of a dynamically allocated copy of the provided string,
 * including its NUL terminator.
 */
void fw_cfg_add_string(FWCfgState *s, uint16_t key, const char *value);

/**
 * fw_cfg_add_i16:
 * @s: fw_cfg device being modified
 * @key: selector key value for new fw_cfg item
 * @value: 16-bit integer
 *
 * Add a new fw_cfg item, available by selecting the given key. The item
 * data will consist of a dynamically allocated copy of the given 16-bit
 * value, converted to little-endian representation.
 */
void fw_cfg_add_i16(FWCfgState *s, uint16_t key, uint16_t value);

/**
 * fw_cfg_modify_i16:
 * @s: fw_cfg device being modified
 * @key: selector key value for new fw_cfg item
 * @value: 16-bit integer
 *
 * Replace the fw_cfg item available by selecting the given key. The new
 * data will consist of a dynamically allocated copy of the given 16-bit
 * value, converted to little-endian representation. The data being replaced,
 * assumed to have been dynamically allocated during an earlier call to
 * either fw_cfg_add_i16() or fw_cfg_modify_i16(), is freed before returning.
 */
void fw_cfg_modify_i16(FWCfgState *s, uint16_t key, uint16_t value);

/**
 * fw_cfg_add_i32:
 * @s: fw_cfg device being modified
 * @key: selector key value for new fw_cfg item
 * @value: 32-bit integer
 *
 * Add a new fw_cfg item, available by selecting the given key. The item
 * data will consist of a dynamically allocated copy of the given 32-bit
 * value, converted to little-endian representation.
 */
void fw_cfg_add_i32(FWCfgState *s, uint16_t key, uint32_t value);

/**
 * fw_cfg_add_i64:
 * @s: fw_cfg device being modified
 * @key: selector key value for new fw_cfg item
 * @value: 64-bit integer
 *
 * Add a new fw_cfg item, available by selecting the given key. The item
 * data will consist of a dynamically allocated copy of the given 64-bit
 * value, converted to little-endian representation.
 */
void fw_cfg_add_i64(FWCfgState *s, uint16_t key, uint64_t value);

/**
 * fw_cfg_add_file:
 * @s: fw_cfg device being modified
 * @filename: name of new fw_cfg file item
 * @data: pointer to start of item data
 * @len: size of item data
 *
 * Add a new NAMED fw_cfg item as a raw "blob" of the given size. The data
 * referenced by the starting pointer is only linked, NOT copied, into the
 * data structure of the fw_cfg device.
 * The next available (unused) selector key starting at FW_CFG_FILE_FIRST
 * will be used; also, a new entry will be added to the file directory
 * structure residing at key value FW_CFG_FILE_DIR, containing the item name,
 * data size, and assigned selector key value.
 */
void fw_cfg_add_file(FWCfgState *s, const char *filename, void *data,
                     size_t len);

/**
 * fw_cfg_add_file_callback:
 * @s: fw_cfg device being modified
 * @filename: name of new fw_cfg file item
 * @callback: callback function
 * @callback_opaque: argument to be passed into callback function
 * @data: pointer to start of item data
 * @len: size of item data
 *
 * Add a new NAMED fw_cfg item as a raw "blob" of the given size. The data
 * referenced by the starting pointer is only linked, NOT copied, into the
 * data structure of the fw_cfg device.
 * The next available (unused) selector key starting at FW_CFG_FILE_FIRST
 * will be used; also, a new entry will be added to the file directory
 * structure residing at key value FW_CFG_FILE_DIR, containing the item name,
 * data size, and assigned selector key value.
 * Additionally, set a callback function (and argument) to be called each
 * time this item is selected (by having its selector key either written to
 * the fw_cfg control register, or passed to QEMU in FWCfgDmaAccess.control
 * with FW_CFG_DMA_CTL_SELECT).
 */
void fw_cfg_add_file_callback(FWCfgState *s, const char *filename,
                              FWCfgReadCallback callback, void *callback_opaque,
                              void *data, size_t len);

/**
 * fw_cfg_modify_file:
 * @s: fw_cfg device being modified
 * @filename: name of new fw_cfg file item
 * @data: pointer to start of item data
 * @len: size of item data
 *
 * Replace a NAMED fw_cfg item. If an existing item is found, its callback
 * information will be cleared, and a pointer to its data will be returned
 * to the caller, so that it may be freed if necessary. If an existing item
 * is not found, this call defaults to fw_cfg_add_file(), and NULL is
 * returned to the caller.
 * In either case, the new item data is only linked, NOT copied, into the
 * data structure of the fw_cfg device.
 *
 * Returns: pointer to old item's data, or NULL if old item does not exist.
 */
void *fw_cfg_modify_file(FWCfgState *s, const char *filename, void *data,
                         size_t len);

FWCfgState *fw_cfg_init_io_dma(uint32_t iobase, uint32_t dma_iobase,
                                AddressSpace *dma_as);
FWCfgState *fw_cfg_init_io(uint32_t iobase);
FWCfgState *fw_cfg_init_mem(hwaddr ctl_addr, hwaddr data_addr);
FWCfgState *fw_cfg_init_mem_wide(hwaddr ctl_addr,
                                 hwaddr data_addr, uint32_t data_width,
                                 hwaddr dma_addr, AddressSpace *dma_as);

FWCfgState *fw_cfg_find(void);

#endif /* NO_QEMU_PROTOS */

#endif
