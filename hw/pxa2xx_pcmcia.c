/*
 * Intel XScale PXA255/270 PC Card and CompactFlash Interface.
 *
 * Copyright (c) 2006 Openedhand Ltd.
 * Written by Andrzej Zaborowski <balrog@zabor.org>
 *
 * This code is licensed under the GPLv2.
 */

#include "hw.h"
#include "pcmcia.h"
#include "pxa.h"

struct PXA2xxPCMCIAState {
    PCMCIASocket slot;
    PCMCIACardState *card;

    qemu_irq irq;
    qemu_irq cd_irq;
};

static uint32_t pxa2xx_pcmcia_common_read(void *opaque,
                target_phys_addr_t offset)
{
    PXA2xxPCMCIAState *s = (PXA2xxPCMCIAState *) opaque;

    if (s->slot.attached) {
        return s->card->common_read(s->card->state, offset);
    }

    return 0;
}

static void pxa2xx_pcmcia_common_write(void *opaque,
                target_phys_addr_t offset, uint32_t value)
{
    PXA2xxPCMCIAState *s = (PXA2xxPCMCIAState *) opaque;

    if (s->slot.attached) {
        s->card->common_write(s->card->state, offset, value);
    }
}

static uint32_t pxa2xx_pcmcia_attr_read(void *opaque,
                target_phys_addr_t offset)
{
    PXA2xxPCMCIAState *s = (PXA2xxPCMCIAState *) opaque;

    if (s->slot.attached) {
        return s->card->attr_read(s->card->state, offset);
    }

    return 0;
}

static void pxa2xx_pcmcia_attr_write(void *opaque,
                target_phys_addr_t offset, uint32_t value)
{
    PXA2xxPCMCIAState *s = (PXA2xxPCMCIAState *) opaque;

    if (s->slot.attached) {
        s->card->attr_write(s->card->state, offset, value);
    }
}

static uint32_t pxa2xx_pcmcia_io_read(void *opaque,
                target_phys_addr_t offset)
{
    PXA2xxPCMCIAState *s = (PXA2xxPCMCIAState *) opaque;

    if (s->slot.attached) {
        return s->card->io_read(s->card->state, offset);
    }

    return 0;
}

static void pxa2xx_pcmcia_io_write(void *opaque,
                target_phys_addr_t offset, uint32_t value)
{
    PXA2xxPCMCIAState *s = (PXA2xxPCMCIAState *) opaque;

    if (s->slot.attached) {
        s->card->io_write(s->card->state, offset, value);
    }
}

static CPUReadMemoryFunc * const pxa2xx_pcmcia_common_readfn[] = {
    pxa2xx_pcmcia_common_read,
    pxa2xx_pcmcia_common_read,
    pxa2xx_pcmcia_common_read,
};

static CPUWriteMemoryFunc * const pxa2xx_pcmcia_common_writefn[] = {
    pxa2xx_pcmcia_common_write,
    pxa2xx_pcmcia_common_write,
    pxa2xx_pcmcia_common_write,
};

static CPUReadMemoryFunc * const pxa2xx_pcmcia_attr_readfn[] = {
    pxa2xx_pcmcia_attr_read,
    pxa2xx_pcmcia_attr_read,
    pxa2xx_pcmcia_attr_read,
};

static CPUWriteMemoryFunc * const pxa2xx_pcmcia_attr_writefn[] = {
    pxa2xx_pcmcia_attr_write,
    pxa2xx_pcmcia_attr_write,
    pxa2xx_pcmcia_attr_write,
};

static CPUReadMemoryFunc * const pxa2xx_pcmcia_io_readfn[] = {
    pxa2xx_pcmcia_io_read,
    pxa2xx_pcmcia_io_read,
    pxa2xx_pcmcia_io_read,
};

static CPUWriteMemoryFunc * const pxa2xx_pcmcia_io_writefn[] = {
    pxa2xx_pcmcia_io_write,
    pxa2xx_pcmcia_io_write,
    pxa2xx_pcmcia_io_write,
};

static void pxa2xx_pcmcia_set_irq(void *opaque, int line, int level)
{
    PXA2xxPCMCIAState *s = (PXA2xxPCMCIAState *) opaque;
    if (!s->irq)
        return;

    qemu_set_irq(s->irq, level);
}

PXA2xxPCMCIAState *pxa2xx_pcmcia_init(target_phys_addr_t base)
{
    int iomemtype;
    PXA2xxPCMCIAState *s;

    s = (PXA2xxPCMCIAState *)
            qemu_mallocz(sizeof(PXA2xxPCMCIAState));

    /* Socket I/O Memory Space */
    iomemtype = cpu_register_io_memory(pxa2xx_pcmcia_io_readfn,
                    pxa2xx_pcmcia_io_writefn, s, DEVICE_NATIVE_ENDIAN);
    cpu_register_physical_memory(base | 0x00000000, 0x04000000, iomemtype);

    /* Then next 64 MB is reserved */

    /* Socket Attribute Memory Space */
    iomemtype = cpu_register_io_memory(pxa2xx_pcmcia_attr_readfn,
                    pxa2xx_pcmcia_attr_writefn, s, DEVICE_NATIVE_ENDIAN);
    cpu_register_physical_memory(base | 0x08000000, 0x04000000, iomemtype);

    /* Socket Common Memory Space */
    iomemtype = cpu_register_io_memory(pxa2xx_pcmcia_common_readfn,
                    pxa2xx_pcmcia_common_writefn, s, DEVICE_NATIVE_ENDIAN);
    cpu_register_physical_memory(base | 0x0c000000, 0x04000000, iomemtype);

    if (base == 0x30000000)
        s->slot.slot_string = "PXA PC Card Socket 1";
    else
        s->slot.slot_string = "PXA PC Card Socket 0";
    s->slot.irq = qemu_allocate_irqs(pxa2xx_pcmcia_set_irq, s, 1)[0];
    pcmcia_socket_register(&s->slot);

    return s;
}

/* Insert a new card into a slot */
int pxa2xx_pcmcia_attach(void *opaque, PCMCIACardState *card)
{
    PXA2xxPCMCIAState *s = (PXA2xxPCMCIAState *) opaque;
    if (s->slot.attached)
        return -EEXIST;

    if (s->cd_irq) {
        qemu_irq_raise(s->cd_irq);
    }

    s->card = card;

    s->slot.attached = 1;
    s->card->slot = &s->slot;
    s->card->attach(s->card->state);

    return 0;
}

/* Eject card from the slot */
int pxa2xx_pcmcia_dettach(void *opaque)
{
    PXA2xxPCMCIAState *s = (PXA2xxPCMCIAState *) opaque;
    if (!s->slot.attached)
        return -ENOENT;

    s->card->detach(s->card->state);
    s->card->slot = NULL;
    s->card = NULL;

    s->slot.attached = 0;

    if (s->irq)
        qemu_irq_lower(s->irq);
    if (s->cd_irq)
        qemu_irq_lower(s->cd_irq);

    return 0;
}

/* Who to notify on card events */
void pxa2xx_pcmcia_set_irq_cb(void *opaque, qemu_irq irq, qemu_irq cd_irq)
{
    PXA2xxPCMCIAState *s = (PXA2xxPCMCIAState *) opaque;
    s->irq = irq;
    s->cd_irq = cd_irq;
}
