
struct cris_load_info
{
    const char *image_filename;
    const char *cmdline;
    int image_size;

    target_phys_addr_t entry;
};

void cris_load_image(CPUState *env, struct cris_load_info *li);
