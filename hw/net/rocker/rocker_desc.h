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


#ifndef _ROCKER_DESC_H_
#define _ROCKER_DESC_H_

#include "rocker_hw.h"

typedef int (desc_ring_consume)(Rocker *r, DescInfo *info);

uint16_t desc_buf_size(DescInfo *info);
uint16_t desc_tlv_size(DescInfo *info);
char *desc_get_buf(DescInfo *info, bool read_only);
int desc_set_buf(DescInfo *info, size_t tlv_size);
DescRing *desc_get_ring(DescInfo *info);

int desc_ring_index(DescRing *ring);
bool desc_ring_set_base_addr(DescRing *ring, uint64_t base_addr);
uint64_t desc_ring_get_base_addr(DescRing *ring);
bool desc_ring_set_size(DescRing *ring, uint32_t size);
uint32_t desc_ring_get_size(DescRing *ring);
bool desc_ring_set_head(DescRing *ring, uint32_t new);
uint32_t desc_ring_get_head(DescRing *ring);
uint32_t desc_ring_get_tail(DescRing *ring);
void desc_ring_set_ctrl(DescRing *ring, uint32_t val);
bool desc_ring_ret_credits(DescRing *ring, uint32_t credits);
uint32_t desc_ring_get_credits(DescRing *ring);

DescInfo *desc_ring_fetch_desc(DescRing *ring);
bool desc_ring_post_desc(DescRing *ring, int status);

void desc_ring_set_consume(DescRing *ring, desc_ring_consume *consume,
                           unsigned vector);
unsigned desc_ring_get_msix_vector(DescRing *ring);
DescRing *desc_ring_alloc(Rocker *r, int index);
void desc_ring_free(DescRing *ring);
void desc_ring_reset(DescRing *ring);

#endif
