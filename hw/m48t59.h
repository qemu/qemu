#if !defined (__M48T59_H__)
#define __M48T59_H__

typedef struct m48t59_t m48t59_t;

void m48t59_write (m48t59_t *NVRAM, uint32_t addr, uint32_t val);
uint32_t m48t59_read (m48t59_t *NVRAM, uint32_t addr);
void m48t59_toggle_lock (m48t59_t *NVRAM, int lock);
m48t59_t *m48t59_init (int IRQ, target_ulong mem_base,
                       uint32_t io_base, uint16_t size,
                       int type);

#endif /* !defined (__M48T59_H__) */
