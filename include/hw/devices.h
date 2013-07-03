#ifndef QEMU_DEVICES_H
#define QEMU_DEVICES_H

#include "hw/irq.h"

/* ??? Not all users of this file can include cpu-common.h.  */
struct MemoryRegion;

/* Devices that have nowhere better to go.  */

/* smc91c111.c */
void smc91c111_init(NICInfo *, uint32_t, qemu_irq);

/* lan9118.c */
void lan9118_init(NICInfo *, uint32_t, qemu_irq);

/* tsc210x.c */
uWireSlave *tsc2102_init(qemu_irq pint);
uWireSlave *tsc2301_init(qemu_irq penirq, qemu_irq kbirq, qemu_irq dav);
I2SCodec *tsc210x_codec(uWireSlave *chip);
uint32_t tsc210x_txrx(void *opaque, uint32_t value, int len);
void tsc210x_set_transform(uWireSlave *chip,
                MouseTransformInfo *info);
void tsc210x_key_event(uWireSlave *chip, int key, int down);

/* tsc2005.c */
void *tsc2005_init(qemu_irq pintdav);
uint32_t tsc2005_txrx(void *opaque, uint32_t value, int len);
void tsc2005_set_transform(void *opaque, MouseTransformInfo *info);

/* stellaris_input.c */
void stellaris_gamepad_init(int n, qemu_irq *irq, const int *keycode);

/* blizzard.c */
void *s1d13745_init(qemu_irq gpio_int);
void s1d13745_write(void *opaque, int dc, uint16_t value);
void s1d13745_write_block(void *opaque, int dc,
                void *buf, size_t len, int pitch);
uint16_t s1d13745_read(void *opaque, int dc);

/* cbus.c */
typedef struct {
    qemu_irq clk;
    qemu_irq dat;
    qemu_irq sel;
} CBus;
CBus *cbus_init(qemu_irq dat_out);
void cbus_attach(CBus *bus, void *slave_opaque);

void *retu_init(qemu_irq irq, int vilma);
void *tahvo_init(qemu_irq irq, int betty);

void retu_key_event(void *retu, int state);

/* tc6393xb.c */
typedef struct TC6393xbState TC6393xbState;
#define TC6393XB_RAM	0x110000 /* amount of ram for Video and USB */
TC6393xbState *tc6393xb_init(struct MemoryRegion *sysmem,
                             uint32_t base, qemu_irq irq);
void tc6393xb_gpio_out_set(TC6393xbState *s, int line,
                    qemu_irq handler);
qemu_irq *tc6393xb_gpio_in_get(TC6393xbState *s);
qemu_irq tc6393xb_l3v_get(TC6393xbState *s);

/* sm501.c */
void sm501_init(struct MemoryRegion *address_space_mem, uint32_t base,
                uint32_t local_mem_bytes, qemu_irq irq,
                CharDriverState *chr);

#endif
