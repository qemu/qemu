#ifndef FIRMWARE_ABI_H
#define FIRMWARE_ABI_H

/* OpenBIOS NVRAM partition */
struct OpenBIOS_nvpart_v1 {
    uint8_t signature;
    uint8_t checksum;
    uint16_t len; // BE, length divided by 16
    char name[12];
};

#define OPENBIOS_PART_SYSTEM 0x70
#define OPENBIOS_PART_FREE 0x7f

static inline void
OpenBIOS_finish_partition(struct OpenBIOS_nvpart_v1 *header, uint32_t size)
{
    unsigned int i, sum;
    uint8_t *tmpptr;

    // Length divided by 16
    header->len = cpu_to_be16(size >> 4);

    // Checksum
    tmpptr = (uint8_t *)header;
    sum = *tmpptr;
    for (i = 0; i < 14; i++) {
        sum += tmpptr[2 + i];
        sum = (sum + ((sum & 0xff00) >> 8)) & 0xff;
    }
    header->checksum = sum & 0xff;
}

static inline uint32_t
OpenBIOS_set_var(uint8_t *nvram, uint32_t addr, const char *str)
{
    uint32_t len;

    len = strlen(str) + 1;
    memcpy(&nvram[addr], str, len);

    return addr + len;
}

/* Sun IDPROM structure at the end of NVRAM */
struct Sun_nvram {
    uint8_t type;
    uint8_t machine_id;
    uint8_t macaddr[6];
    uint8_t unused[7];
    uint8_t checksum;
};

static inline void
Sun_init_header(struct Sun_nvram *header, const uint8_t *macaddr, int machine_id)
{
    uint8_t tmp, *tmpptr;
    unsigned int i;

    header->type = 1;
    header->machine_id = machine_id & 0xff;
    memcpy(&header->macaddr, macaddr, 6);
    /* Calculate checksum */
    tmp = 0;
    tmpptr = (uint8_t *)header;
    for (i = 0; i < 15; i++)
        tmp ^= tmpptr[i];

    header->checksum = tmp;
}
#endif /* FIRMWARE_ABI_H */
