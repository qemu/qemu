#if !defined (__M48T08_H__)
#define __M48T08_H__

typedef struct m48t08_t m48t08_t;

void m48t08_write (m48t08_t *NVRAM, uint32_t val);
uint32_t m48t08_read (m48t08_t *NVRAM);
void m48t08_set_addr (m48t08_t *NVRAM, uint32_t addr);
void m48t08_toggle_lock (m48t08_t *NVRAM, int lock);
m48t08_t *m48t08_init(uint32_t mem_base, uint16_t size, uint8_t *macaddr);

#endif /* !defined (__M48T08_H__) */
