/*
 * USB quirk handling
 *
 * Copyright (c) 2012 Red Hat, Inc.
 *
 * Red Hat Authors:
 * Hans de Goede <hdegoede@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "quirks.h"
#include "hw/usb.h"

static bool usb_id_match(const struct usb_device_id *ids,
                         uint16_t vendor_id, uint16_t product_id,
                         uint8_t interface_class, uint8_t interface_subclass,
                         uint8_t interface_protocol) {
    int i;

    for (i = 0; ids[i].vendor_id != -1; i++) {
        if (ids[i].vendor_id  == vendor_id &&
            ids[i].product_id == product_id &&
            (ids[i].interface_class == -1 ||
             (ids[i].interface_class == interface_class &&
              ids[i].interface_subclass == interface_subclass &&
              ids[i].interface_protocol == interface_protocol))) {
            return true;
        }
    }
    return false;
}

int usb_get_quirks(uint16_t vendor_id, uint16_t product_id,
                   uint8_t interface_class, uint8_t interface_subclass,
                   uint8_t interface_protocol)
{
    int quirks = 0;

    if (usb_id_match(usbredir_raw_serial_ids, vendor_id, product_id,
                   interface_class, interface_subclass, interface_protocol)) {
        quirks |= USB_QUIRK_BUFFER_BULK_IN;
    }
    if (usb_id_match(usbredir_ftdi_serial_ids, vendor_id, product_id,
                   interface_class, interface_subclass, interface_protocol)) {
        quirks |= USB_QUIRK_BUFFER_BULK_IN | USB_QUIRK_IS_FTDI;
    }

    return quirks;
}
