#ifndef FIRMWARE_ABI_H
#define FIRMWARE_ABI_H

#ifndef __ASSEMBLY__
/* Open Hack'Ware NVRAM configuration structure */

/* Version 3 */
typedef struct ohwcfg_v3_t ohwcfg_v3_t;
struct ohwcfg_v3_t {
    /* 0x00: structure identifier                    */
    uint8_t  struct_ident[0x10];
    /* 0x10: structure version and NVRAM description */
    uint32_t struct_version;
    uint16_t nvram_size;
    uint16_t pad0;
    uint16_t nvram_arch_ptr;
    uint16_t nvram_arch_size;
    uint16_t nvram_arch_crc;
    uint8_t  pad1[0x02];
    /* 0x20: host architecture                       */
    uint8_t  arch[0x10];
    /* 0x30: RAM/ROM description                     */
    uint64_t RAM0_base;
    uint64_t RAM0_size;
    uint64_t RAM1_base;
    uint64_t RAM1_size;
    uint64_t RAM2_base;
    uint64_t RAM2_size;
    uint64_t RAM3_base;
    uint64_t RAM3_size;
    uint64_t ROM_base;
    uint64_t ROM_size;
    /* 0x80: Kernel description                      */
    uint64_t kernel_image;
    uint64_t kernel_size;
    /* 0x90: Kernel command line                     */
    uint64_t cmdline;
    uint64_t cmdline_size;
    /* 0xA0: Kernel boot image                       */
    uint64_t initrd_image;
    uint64_t initrd_size;
    /* 0xB0: NVRAM image                             */
    uint64_t NVRAM_image;
    uint8_t  pad2[8];
    /* 0xC0: graphic configuration                   */
    uint16_t width;
    uint16_t height;
    uint16_t depth;
    uint16_t graphic_flags;
    /* 0xC8: CPUs description                        */
    uint8_t  nb_cpus;
    uint8_t  boot_cpu;
    uint8_t  nboot_devices;
    uint8_t  pad3[5];
    /* 0xD0: boot devices                            */
    uint8_t  boot_devices[0x10];
    /* 0xE0                                          */
    uint8_t  pad4[0x1C]; /* 28 */
    /* 0xFC: checksum                                */
    uint16_t crc;
    uint8_t  pad5[0x02];
} __attribute__ (( packed ));

#define OHW_GF_NOGRAPHICS 0x0001

static inline uint16_t
OHW_crc_update (uint16_t prev, uint16_t value)
{
    uint16_t tmp;
    uint16_t pd, pd1, pd2;

    tmp = prev >> 8;
    pd = prev ^ value;
    pd1 = pd & 0x000F;
    pd2 = ((pd >> 4) & 0x000F) ^ pd1;
    tmp ^= (pd1 << 3) | (pd1 << 8);
    tmp ^= pd2 | (pd2 << 7) | (pd2 << 12);

    return tmp;
}

static inline uint16_t
OHW_compute_crc (ohwcfg_v3_t *header, uint32_t start, uint32_t count)
{
    uint32_t i;
    uint16_t crc = 0xFFFF;
    uint8_t *ptr = (uint8_t *)header;
    int odd;

    odd = count & 1;
    count &= ~1;
    for (i = 0; i != count; i++) {
        crc = OHW_crc_update(crc, (ptr[start + i] << 8) | ptr[start + i + 1]);
    }
    if (odd) {
        crc = OHW_crc_update(crc, ptr[start + i] << 8);
    }

    return crc;
}

/* Sparc32 runtime NVRAM structure for SMP CPU boot */
struct sparc_arch_cfg {
    uint32_t smp_ctx;
    uint32_t smp_ctxtbl;
    uint32_t smp_entry;
    uint8_t valid;
    uint8_t unused[51];
};

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
OpenBIOS_set_var(uint8_t *nvram, uint32_t addr, const unsigned char *str)
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

#else /* __ASSEMBLY__ */

/* Structure offsets for asm use */

/* Open Hack'Ware NVRAM configuration structure */
#define OHW_ARCH_PTR   0x18
#define OHW_RAM_SIZE   0x38
#define OHW_BOOT_CPU   0xC9

/* Sparc32 runtime NVRAM structure for SMP CPU boot */
#define SPARC_SMP_CTX    0x0
#define SPARC_SMP_CTXTBL 0x4
#define SPARC_SMP_ENTRY  0x8
#define SPARC_SMP_VALID  0xc

/* Sun IDPROM structure at the end of NVRAM */
#define SPARC_MACHINE_ID 0x1fd9

#endif /* __ASSEMBLY__ */
#endif /* FIRMWARE_ABI_H */
