#if !defined (__M48T59_H__)
#define __M48T59_H__

typedef struct m48t59_t m48t59_t;

void m48t59_write (m48t59_t *NVRAM, uint32_t val);
uint32_t m48t59_read (m48t59_t *NVRAM);
void m48t59_set_addr (m48t59_t *NVRAM, uint32_t addr);
m48t59_t *m48t59_init (int IRQ, uint32_t io_base, uint16_t size);

#endif /* !defined (__M48T59_H__) */
