#ifndef SUN_NVRAM_H
#define SUN_NVRAM_H

/* Sun IDPROM structure at the end of NVRAM */
/* from http://www.squirrel.com/squirrel/sun-nvram-hostid.faq.html */
struct Sun_nvram {
    uint8_t type;       /* always 01 */
    uint8_t machine_id; /* first byte of host id (machine type) */
    uint8_t macaddr[6]; /* 6 byte ethernet address (first 3 bytes 08, 00, 20) */
    uint8_t date[4];    /* date of manufacture */
    uint8_t hostid[3];  /* remaining 3 bytes of host id (serial number) */
    uint8_t checksum;   /* bitwise xor of previous bytes */
};

static inline void
Sun_init_header(struct Sun_nvram *header, const uint8_t *macaddr, int machine_id)
{
    uint8_t tmp, *tmpptr;
    unsigned int i;

    header->type = 1;
    header->machine_id = machine_id & 0xff;
    memcpy(&header->macaddr, macaddr, 6);
    memcpy(&header->hostid , &macaddr[3], 3);

    /* Calculate checksum */
    tmp = 0;
    tmpptr = (uint8_t *)header;
    for (i = 0; i < 15; i++)
        tmp ^= tmpptr[i];

    header->checksum = tmp;
}
#endif /* SUN_NVRAM_H */
