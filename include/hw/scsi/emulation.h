#ifndef HW_SCSI_EMULATION_H
#define HW_SCSI_EMULATION_H 1

typedef struct SCSIBlockLimits {
    bool wsnz;
    uint16_t min_io_size;
    uint32_t max_unmap_descr;
    uint32_t opt_io_size;
    uint32_t max_unmap_sectors;
    uint32_t unmap_sectors;
    uint32_t max_io_sectors;
} SCSIBlockLimits;

int scsi_emulate_block_limits(uint8_t *outbuf, const SCSIBlockLimits *bl);

#endif
