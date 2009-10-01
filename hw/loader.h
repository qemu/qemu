#ifndef LOADER_H
#define LOADER_H

/* loader.c */
int get_image_size(const char *filename);
int load_image(const char *filename, uint8_t *addr); /* deprecated */
int load_image_targphys(const char *filename, a_target_phys_addr, int max_sz);
int load_elf(const char *filename, int64_t address_offset,
             uint64_t *pentry, uint64_t *lowaddr, uint64_t *highaddr,
             int big_endian, int elf_machine, int clear_lsb);
int load_aout(const char *filename, a_target_phys_addr addr, int max_sz,
              int bswap_needed, a_target_phys_addr target_page_size);
int load_uimage(const char *filename, a_target_phys_addr *ep,
                a_target_phys_addr *loadaddr, int *is_linux);

int fread_targphys(a_target_phys_addr dst_addr, size_t nbytes, FILE *f);
int fread_targphys_ok(a_target_phys_addr dst_addr, size_t nbytes, FILE *f);
int read_targphys(int fd, a_target_phys_addr dst_addr, size_t nbytes);
void pstrcpy_targphys(a_target_phys_addr dest, int buf_size,
                      const char *source);
#endif
