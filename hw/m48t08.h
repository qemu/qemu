#if !defined (__M48T08_H__)
#define __M48T08_H__

typedef struct m48t08_t m48t08_t;

void m48t08_write (m48t08_t *NVRAM, uint32_t addr, uint8_t val);
uint8_t m48t08_read (m48t08_t *NVRAM, uint32_t addr);
m48t08_t *m48t08_init(uint32_t mem_base, uint16_t size);

#endif /* !defined (__M48T08_H__) */
