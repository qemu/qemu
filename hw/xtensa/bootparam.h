#ifndef HW_XTENSA_BOOTPARAM
#define HW_XTENSA_BOOTPARAM

typedef struct BpTag {
    uint16_t tag;
    uint16_t size;
} BpTag;

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
