#ifndef EDID_H
#define EDID_H

typedef struct qemu_edid_info {
    const char *vendor;
    const char *name;
    const char *serial;
    uint32_t    dpi;
    uint32_t    prefx;
    uint32_t    prefy;
    uint32_t    maxx;
    uint32_t    maxy;
} qemu_edid_info;

void qemu_edid_generate(uint8_t *edid, size_t size,
                        qemu_edid_info *info);

#endif /* EDID_H */
