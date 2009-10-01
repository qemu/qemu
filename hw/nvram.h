#ifndef NVRAM_H
#define NVRAM_H

/* NVRAM helpers */
typedef uint32_t (*a_nvram_read)(void *private, uint32_t addr);
typedef void (*a_nvram_write)(void *private, uint32_t addr, uint32_t val);
typedef struct nvram {
    void *opaque;
    a_nvram_read read_fn;
    a_nvram_write write_fn;
} a_nvram;

void NVRAM_set_byte (a_nvram *nvram, uint32_t addr, uint8_t value);
uint8_t NVRAM_get_byte (a_nvram *nvram, uint32_t addr);
void NVRAM_set_word (a_nvram *nvram, uint32_t addr, uint16_t value);
uint16_t NVRAM_get_word (a_nvram *nvram, uint32_t addr);
void NVRAM_set_lword (a_nvram *nvram, uint32_t addr, uint32_t value);
uint32_t NVRAM_get_lword (a_nvram *nvram, uint32_t addr);
void NVRAM_set_string (a_nvram *nvram, uint32_t addr,
                       const char *str, uint32_t max);
int NVRAM_get_string (a_nvram *nvram, uint8_t *dst, uint16_t addr, int max);
void NVRAM_set_crc (a_nvram *nvram, uint32_t addr,
                    uint32_t start, uint32_t count);
int PPC_NVRAM_set_params (a_nvram *nvram, uint16_t NVRAM_size,
                          const char *arch,
                          uint32_t RAM_size, int boot_device,
                          uint32_t kernel_image, uint32_t kernel_size,
                          const char *cmdline,
                          uint32_t initrd_image, uint32_t initrd_size,
                          uint32_t NVRAM_image,
                          int width, int height, int depth);
typedef struct m48t59 a_m48t59;

void m48t59_write (void *private, uint32_t addr, uint32_t val);
uint32_t m48t59_read (void *private, uint32_t addr);
void m48t59_toggle_lock (void *private, int lock);
a_m48t59 *m48t59_init_isa(uint32_t io_base, uint16_t size, int type);
a_m48t59 *m48t59_init (qemu_irq IRQ, a_target_phys_addr mem_base,
                       uint32_t io_base, uint16_t size,
                       int type);
void m48t59_set_addr (void *opaque, uint32_t addr);

#endif /* !NVRAM_H */
