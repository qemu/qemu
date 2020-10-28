#ifndef HW_CRIS_BOOT_H
#define HW_CRIS_BOOT_H

struct cris_load_info
{
    const char *image_filename;
    const char *cmdline;
    int image_size;
    ram_addr_t ram_size;

    hwaddr entry;
};

void cris_load_image(CRISCPU *cpu, struct cris_load_info *li);

#endif
