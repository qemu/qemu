/*
 * "Inventra" High-speed Dual-Role Controller (MUSB-HDRC), Mentor Graphics,
 * USB2.0 OTG compliant core used in various chips.
 *
 * Only host-mode and non-DMA accesses are currently supported.
 *
 * Copyright (C) 2008 Nokia Corporation
 * Written by Andrzej Zaborowski <balrog@zabor.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_USB_HCD_MUSB_H
#define HW_USB_HCD_MUSB_H

enum musb_irq_source_e {
    musb_irq_suspend = 0,
    musb_irq_resume,
    musb_irq_rst_babble,
    musb_irq_sof,
    musb_irq_connect,
    musb_irq_disconnect,
    musb_irq_vbus_request,
    musb_irq_vbus_error,
    musb_irq_rx,
    musb_irq_tx,
    musb_set_vbus,
    musb_set_session,
    /* Add new interrupts here */
    musb_irq_max /* total number of interrupts defined */
};

/* TODO convert hcd-musb to QOM/qdev and remove MUSBReadFunc/MUSBWriteFunc */
typedef void MUSBWriteFunc(void *opaque, hwaddr addr, uint32_t value);
typedef uint32_t MUSBReadFunc(void *opaque, hwaddr addr);
extern MUSBReadFunc * const musb_read[];
extern MUSBWriteFunc * const musb_write[];

typedef struct MUSBState MUSBState;

MUSBState *musb_init(DeviceState *parent_device, int gpio_base);
void musb_reset(MUSBState *s);
uint32_t musb_core_intr_get(MUSBState *s);
void musb_core_intr_clear(MUSBState *s, uint32_t mask);
void musb_set_size(MUSBState *s, int epnum, int size, int is_tx);

#endif
