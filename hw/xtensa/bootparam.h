#ifndef HW_XTENSA_BOOTPARAM
#define HW_XTENSA_BOOTPARAM

#define BP_TAG_COMMAND_LINE     0x1001  /* command line (0-terminated string)*/
#define BP_TAG_INITRD           0x1002  /* ramdisk addr and size (bp_meminfo) */
#define BP_TAG_MEMORY           0x1003  /* memory addr and size (bp_meminfo) */
#define BP_TAG_SERIAL_BAUDRATE  0x1004  /* baud rate of current console. */
#define BP_TAG_SERIAL_PORT      0x1005  /* serial device of current console */
#define BP_TAG_FDT              0x1006  /* flat device tree addr */

#define BP_TAG_FIRST            0x7B0B  /* first tag with a version number */
#define BP_TAG_LAST             0x7E0B  /* last tag */

typedef struct BpTag {
    uint16_t tag;
    uint16_t size;
} BpTag;

static inline size_t get_tag_size(size_t data_size)
{
    return data_size + sizeof(BpTag) + 4;
}

static inline ram_addr_t put_tag(ram_addr_t addr, uint16_t tag,
        size_t size, const void *data)
{
    BpTag bp_tag = {
        .tag = tswap16(tag),
        .size = tswap16((size + 3) & ~3),
    };

    cpu_physical_memory_write(addr, &bp_tag, sizeof(bp_tag));
    addr += sizeof(bp_tag);
    cpu_physical_memory_write(addr, data, size);
    addr += (size + 3) & ~3;

    return addr;
}

#endif
