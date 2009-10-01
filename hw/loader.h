#ifndef LOADER_H
#define LOADER_H

/* loader.c */
int get_image_size(const char *filename);
int load_image(const char *filename, uint8_t *addr); /* deprecated */
int load_image_targphys(const char *filename, target_phys_addr_t, int max_sz);
int load_elf(const char *filename, int64_t address_offset,
             uint64_t *pentry, uint64_t *lowaddr, uint64_t *highaddr,
             int big_endian, int elf_machine, int clear_lsb);
int load_aout(const char *filename, target_phys_addr_t addr, int max_sz,
              int bswap_needed, target_phys_addr_t target_page_size);
int load_uimage(const char *filename, target_phys_addr_t *ep,
                target_phys_addr_t *loadaddr, int *is_linux);

int fread_targphys(target_phys_addr_t dst_addr, size_t nbytes, FILE *f);
int fread_targphys_ok(target_phys_addr_t dst_addr, size_t nbytes, FILE *f);
int read_targphys(int fd, target_phys_addr_t dst_addr, size_t nbytes);
void pstrcpy_targphys(target_phys_addr_t dest, int buf_size,
                      const char *source);
#endif
