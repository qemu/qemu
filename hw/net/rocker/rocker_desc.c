/*
 * QEMU rocker switch emulation - Descriptor ring support
 *
 * Copyright (c) 2014 Scott Feldman <sfeldma@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include "qemu/osdep.h"
#include "net/net.h"
#include "hw/pci/pci_device.h"

#include "rocker.h"
#include "rocker_hw.h"
#include "rocker_desc.h"

struct desc_ring {
    hwaddr base_addr;
    uint32_t size;
    uint32_t head;
    uint32_t tail;
    uint32_t ctrl;
    uint32_t credits;
    Rocker *r;
    DescInfo *info;
    int index;
    desc_ring_consume *consume;
    unsigned msix_vector;
};

struct desc_info {
    DescRing *ring;
    RockerDesc desc;
    char *buf;
    size_t buf_size;
};

uint16_t desc_buf_size(DescInfo *info)
{
    return le16_to_cpu(info->desc.buf_size);
}

uint16_t desc_tlv_size(DescInfo *info)
{
    return le16_to_cpu(info->desc.tlv_size);
}

char *desc_get_buf(DescInfo *info, bool read_only)
{
    PCIDevice *dev = PCI_DEVICE(info->ring->r);
    size_t size = read_only ? le16_to_cpu(info->desc.tlv_size) :
                              le16_to_cpu(info->desc.buf_size);

    if (size > info->buf_size) {
        info->buf = g_realloc(info->buf, size);
        info->buf_size = size;
    }

    pci_dma_read(dev, le64_to_cpu(info->desc.buf_addr), info->buf, size);

    return info->buf;
}

int desc_set_buf(DescInfo *info, size_t tlv_size)
{
    PCIDevice *dev = PCI_DEVICE(info->ring->r);

    if (tlv_size > info->buf_size) {
        DPRINTF("ERROR: trying to write more to desc buf than it "
                "can hold buf_size %zu tlv_size %zu\n",
                info->buf_size, tlv_size);
        return -ROCKER_EMSGSIZE;
    }

    info->desc.tlv_size = cpu_to_le16(tlv_size);
    pci_dma_write(dev, le64_to_cpu(info->desc.buf_addr), info->buf, tlv_size);

    return ROCKER_OK;
}

DescRing *desc_get_ring(DescInfo *info)
{
    return info->ring;
}

int desc_ring_index(DescRing *ring)
{
    return ring->index;
}

static bool desc_ring_empty(DescRing *ring)
{
    return ring->head == ring->tail;
}

bool desc_ring_set_base_addr(DescRing *ring, uint64_t base_addr)
{
    if (base_addr & 0x7) {
        DPRINTF("ERROR: ring[%d] desc base addr (0x" HWADDR_FMT_plx
                ") not 8-byte aligned\n", ring->index, base_addr);
        return false;
    }

    ring->base_addr = base_addr;

    return true;
}

uint64_t desc_ring_get_base_addr(DescRing *ring)
{
    return ring->base_addr;
}

bool desc_ring_set_size(DescRing *ring, uint32_t size)
{
    int i;

    if (size < 2 || size > 0x10000 || (size & (size - 1))) {
        DPRINTF("ERROR: ring[%d] size (%d) not a power of 2 "
                "or in range [2, 64K]\n", ring->index, size);
        return false;
    }

    for (i = 0; i < ring->size; i++) {
        g_free(ring->info[i].buf);
    }

    ring->size = size;
    ring->head = ring->tail = 0;

    ring->info = g_renew(DescInfo, ring->info, size);

    memset(ring->info, 0, size * sizeof(DescInfo));

    for (i = 0; i < size; i++) {
        ring->info[i].ring = ring;
    }

    return true;
}

uint32_t desc_ring_get_size(DescRing *ring)
{
    return ring->size;
}

static DescInfo *desc_read(DescRing *ring, uint32_t index)
{
    PCIDevice *dev = PCI_DEVICE(ring->r);
    DescInfo *info = &ring->info[index];
    hwaddr addr = ring->base_addr + (sizeof(RockerDesc) * index);

    pci_dma_read(dev, addr, &info->desc, sizeof(info->desc));

    return info;
}

static void desc_write(DescRing *ring, uint32_t index)
{
    PCIDevice *dev = PCI_DEVICE(ring->r);
    DescInfo *info = &ring->info[index];
    hwaddr addr = ring->base_addr + (sizeof(RockerDesc) * index);

    pci_dma_write(dev, addr, &info->desc, sizeof(info->desc));
}

static bool desc_ring_base_addr_check(DescRing *ring)
{
    if (!ring->base_addr) {
        DPRINTF("ERROR: ring[%d] not-initialized desc base address!\n",
                ring->index);
        return false;
    }
    return true;
}

static DescInfo *__desc_ring_fetch_desc(DescRing *ring)
{
    return desc_read(ring, ring->tail);
}

DescInfo *desc_ring_fetch_desc(DescRing *ring)
{
    if (desc_ring_empty(ring) || !desc_ring_base_addr_check(ring)) {
        return NULL;
    }

    return desc_read(ring, ring->tail);
}

static bool __desc_ring_post_desc(DescRing *ring, int err)
{
    uint16_t comp_err = 0x8000 | (uint16_t)-err;
    DescInfo *info = &ring->info[ring->tail];

    info->desc.comp_err = cpu_to_le16(comp_err);
    desc_write(ring, ring->tail);
    ring->tail = (ring->tail + 1) % ring->size;

    /* return true if starting credit count */

    return ring->credits++ == 0;
}

bool desc_ring_post_desc(DescRing *ring, int err)
{
    if (desc_ring_empty(ring)) {
        DPRINTF("ERROR: ring[%d] trying to post desc to empty ring\n",
                ring->index);
        return false;
    }

    if (!desc_ring_base_addr_check(ring)) {
        return false;
    }

    return __desc_ring_post_desc(ring, err);
}

static bool ring_pump(DescRing *ring)
{
    DescInfo *info;
    bool primed = false;
    int err;

    /* If the ring has a consumer, call consumer for each
     * desc starting at tail and stopping when tail reaches
     * head (the empty ring condition).
     */

    if (ring->consume) {
        while (ring->head != ring->tail) {
            info = __desc_ring_fetch_desc(ring);
            err = ring->consume(ring->r, info);
            if (__desc_ring_post_desc(ring, err)) {
                primed = true;
            }
        }
    }

    return primed;
}

bool desc_ring_set_head(DescRing *ring, uint32_t new)
{
    uint32_t tail = ring->tail;
    uint32_t head = ring->head;

    if (!desc_ring_base_addr_check(ring)) {
        return false;
    }

    if (new >= ring->size) {
        DPRINTF("ERROR: trying to set head (%d) past ring[%d] size (%d)\n",
                new, ring->index, ring->size);
        return false;
    }

    if (((head < tail) && ((new >= tail) || (new < head))) ||
        ((head > tail) && ((new >= tail) && (new < head)))) {
        DPRINTF("ERROR: trying to wrap ring[%d] "
                "(head %d, tail %d, new head %d)\n",
                ring->index, head, tail, new);
        return false;
    }

    if (new == ring->head) {
        DPRINTF("WARNING: setting head (%d) to current head position\n", new);
    }

    ring->head = new;

    return ring_pump(ring);
}

uint32_t desc_ring_get_head(DescRing *ring)
{
    return ring->head;
}

uint32_t desc_ring_get_tail(DescRing *ring)
{
    return ring->tail;
}

void desc_ring_set_ctrl(DescRing *ring, uint32_t val)
{
    if (val & ROCKER_DMA_DESC_CTRL_RESET) {
        DPRINTF("ring[%d] resetting\n", ring->index);
        desc_ring_reset(ring);
    }
}

bool desc_ring_ret_credits(DescRing *ring, uint32_t credits)
{
    if (credits > ring->credits) {
        DPRINTF("ERROR: trying to return more credits (%d) "
                "than are outstanding (%d)\n", credits, ring->credits);
        ring->credits = 0;
        return false;
    }

    ring->credits -= credits;

    /* return true if credits are still outstanding */

    return ring->credits > 0;
}

uint32_t desc_ring_get_credits(DescRing *ring)
{
    return ring->credits;
}

void desc_ring_set_consume(DescRing *ring, desc_ring_consume *consume,
                           unsigned vector)
{
    ring->consume = consume;
    ring->msix_vector = vector;
}

unsigned desc_ring_get_msix_vector(DescRing *ring)
{
    return ring->msix_vector;
}

DescRing *desc_ring_alloc(Rocker *r, int index)
{
    DescRing *ring;

    ring = g_new0(DescRing, 1);

    ring->r = r;
    ring->index = index;

    return ring;
}

void desc_ring_free(DescRing *ring)
{
    g_free(ring->info);
    g_free(ring);
}

void desc_ring_reset(DescRing *ring)
{
    ring->base_addr = 0;
    ring->size = 0;
    ring->head = 0;
    ring->tail = 0;
    ring->ctrl = 0;
    ring->credits = 0;
}
