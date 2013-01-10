#ifndef NVRAM_H
#define NVRAM_H

/* NVRAM helpers */
typedef uint32_t (*nvram_read_t)(void *private, uint32_t addr);
typedef void (*nvram_write_t)(void *private, uint32_t addr, uint32_t val);
typedef struct nvram_t {
    void *opaque;
    nvram_read_t read_fn;
    nvram_write_t write_fn;
} nvram_t;

uint32_t NVRAM_get_lword (nvram_t *nvram, uint32_t addr);
int NVRAM_get_string (nvram_t *nvram, uint8_t *dst, uint16_t addr, int max);

int PPC_NVRAM_set_params (nvram_t *nvram, uint16_t NVRAM_size,
                          const char *arch,
                          uint32_t RAM_size, int boot_device,
                          uint32_t kernel_image, uint32_t kernel_size,
                          const char *cmdline,
                          uint32_t initrd_image, uint32_t initrd_size,
                          uint32_t NVRAM_image,
                          int width, int height, int depth);
typedef struct M48t59State M48t59State;

void m48t59_write (void *private, uint32_t addr, uint32_t val);
uint32_t m48t59_read (void *private, uint32_t addr);
void m48t59_toggle_lock (void *private, int lock);
M48t59State *m48t59_init_isa(ISABus *bus, uint32_t io_base, uint16_t size,
                             int type);
M48t59State *m48t59_init(qemu_irq IRQ, hwaddr mem_base,
                         uint32_t io_base, uint16_t size, int type);

#endif /* !NVRAM_H */
