#if !defined (__M48T59_H__)
#define __M48T59_H__

void m48t59_write (void *opaque, uint32_t val);
uint32_t m48t59_read (void *opaque);
void m48t59_set_addr (void *opaque, uint32_t addr);
void *m48t59_init (int IRQ, uint32_t io_base, uint16_t size);

#endif /* !defined (__M48T59_H__) */
