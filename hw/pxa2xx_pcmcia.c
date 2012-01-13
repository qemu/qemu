/*
 * Intel XScale PXA255/270 PC Card and CompactFlash Interface.
 *
 * Copyright (c) 2006 Openedhand Ltd.
 * Written by Andrzej Zaborowski <balrog@zabor.org>
 *
 * This code is licensed under the GPLv2.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "hw.h"
#include "pcmcia.h"
#include "pxa.h"


struct PXA2xxPCMCIAState {
    PCMCIASocket slot;
    PCMCIACardState *card;
    MemoryRegion common_iomem;
    MemoryRegion attr_iomem;
    MemoryRegion iomem;

    qemu_irq irq;
    qemu_irq cd_irq;
};

static uint64_t pxa2xx_pcmcia_common_read(void *opaque,
                target_phys_addr_t offset, unsigned size)
{
    PXA2xxPCMCIAState *s = (PXA2xxPCMCIAState *) opaque;

    if (s->slot.attached) {
        return s->card->common_read(s->card->state, offset);
    }

    return 0;
}

static void pxa2xx_pcmcia_common_write(void *opaque, target_phys_addr_t offset,
                                       uint64_t value, unsigned size)
{
    PXA2xxPCMCIAState *s = (PXA2xxPCMCIAState *) opaque;

    if (s->slot.attached) {
        s->card->common_write(s->card->state, offset, value);
    }
}

static uint64_t pxa2xx_pcmcia_attr_read(void *opaque,
                target_phys_addr_t offset, unsigned size)
{
    PXA2xxPCMCIAState *s = (PXA2xxPCMCIAState *) opaque;

    if (s->slot.attached) {
        return s->card->attr_read(s->card->state, offset);
    }

    return 0;
}

static void pxa2xx_pcmcia_attr_write(void *opaque, target_phys_addr_t offset,
                                     uint64_t value, unsigned size)
{
    PXA2xxPCMCIAState *s = (PXA2xxPCMCIAState *) opaque;

    if (s->slot.attached) {
        s->card->attr_write(s->card->state, offset, value);
    }
}

static uint64_t pxa2xx_pcmcia_io_read(void *opaque,
                target_phys_addr_t offset, unsigned size)
{
    PXA2xxPCMCIAState *s = (PXA2xxPCMCIAState *) opaque;

    if (s->slot.attached) {
        return s->card->io_read(s->card->state, offset);
    }

    return 0;
}

static void pxa2xx_pcmcia_io_write(void *opaque, target_phys_addr_t offset,
                                   uint64_t value, unsigned size)
{
    PXA2xxPCMCIAState *s = (PXA2xxPCMCIAState *) opaque;

    if (s->slot.attached) {
        s->card->io_write(s->card->state, offset, value);
    }
}

static const MemoryRegionOps pxa2xx_pcmcia_common_ops = {
    .read = pxa2xx_pcmcia_common_read,
    .write = pxa2xx_pcmcia_common_write,
    .endianness = DEVICE_NATIVE_ENDIAN
};

static const MemoryRegionOps pxa2xx_pcmcia_attr_ops = {
    .read = pxa2xx_pcmcia_attr_read,
    .write = pxa2xx_pcmcia_attr_write,
    .endianness = DEVICE_NATIVE_ENDIAN
};

static const MemoryRegionOps pxa2xx_pcmcia_io_ops = {
    .read = pxa2xx_pcmcia_io_read,
    .write = pxa2xx_pcmcia_io_write,
    .endianness = DEVICE_NATIVE_ENDIAN
};

static void pxa2xx_pcmcia_set_irq(void *opaque, int line, int level)
{
    PXA2xxPCMCIAState *s = (PXA2xxPCMCIAState *) opaque;
    if (!s->irq)
        return;

    qemu_set_irq(s->irq, level);
}

PXA2xxPCMCIAState *pxa2xx_pcmcia_init(MemoryRegion *sysmem,
                                      target_phys_addr_t base)
{
    PXA2xxPCMCIAState *s;

    s = (PXA2xxPCMCIAState *)
            g_malloc0(sizeof(PXA2xxPCMCIAState));

    /* Socket I/O Memory Space */
    memory_region_init_io(&s->iomem, &pxa2xx_pcmcia_io_ops, s,
                          "pxa2xx-pcmcia-io", 0x04000000);
    memory_region_add_subregion(sysmem, base | 0x00000000,
                                &s->iomem);

    /* Then next 64 MB is reserved */

    /* Socket Attribute Memory Space */
    memory_region_init_io(&s->attr_iomem, &pxa2xx_pcmcia_attr_ops, s,
                          "pxa2xx-pcmcia-attribute", 0x04000000);
    memory_region_add_subregion(sysmem, base | 0x08000000,
                                &s->attr_iomem);

    /* Socket Common Memory Space */
    memory_region_init_io(&s->common_iomem, &pxa2xx_pcmcia_common_ops, s,
                          "pxa2xx-pcmcia-common", 0x04000000);
    memory_region_add_subregion(sysmem, base | 0x0c000000,
                                &s->common_iomem);

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
