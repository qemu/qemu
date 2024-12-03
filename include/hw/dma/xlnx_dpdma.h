/*
 * xlnx_dpdma.h
 *
 *  Copyright (C) 2015 : GreenSocs Ltd
 *      http://www.greensocs.com/ , email: info@greensocs.com
 *
 *  Developed by :
 *  Frederic Konrad   <fred.konrad@greensocs.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef XLNX_DPDMA_H
#define XLNX_DPDMA_H

#include "hw/sysbus.h"
#include "ui/console.h"
#include "system/dma.h"
#include "qom/object.h"

#define XLNX_DPDMA_REG_ARRAY_SIZE (0x1000 >> 2)

struct XlnxDPDMAState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/
    MemoryRegion iomem;
    uint32_t registers[XLNX_DPDMA_REG_ARRAY_SIZE];
    uint8_t *data[6];
    bool operation_finished[6];
    qemu_irq irq;
};


#define TYPE_XLNX_DPDMA "xlnx.dpdma"
OBJECT_DECLARE_SIMPLE_TYPE(XlnxDPDMAState, XLNX_DPDMA)

/*
 * xlnx_dpdma_start_operation: Start the operation on the specified channel. The
 *                             DPDMA gets the current descriptor and retrieves
 *                             data to the buffer specified by
 *                             dpdma_set_host_data_location().
 *
 * Returns The number of bytes transferred by the DPDMA
 *         or 0 if an error occurred.
 *
 * @s The DPDMA state.
 * @channel The channel to start.
 */
size_t xlnx_dpdma_start_operation(XlnxDPDMAState *s, uint8_t channel,
                                  bool one_desc);

/*
 * xlnx_dpdma_set_host_data_location: Set the location in the host memory where
 *                                    to store the data out from the dma
 *                                    channel.
 *
 * @s The DPDMA state.
 * @channel The channel associated to the pointer.
 * @p The buffer where to store the data.
 */
/* XXX: add a maximum size arg and send an interrupt in case of overflow. */
void xlnx_dpdma_set_host_data_location(XlnxDPDMAState *s, uint8_t channel,
                                       void *p);

/*
 * xlnx_dpdma_trigger_vsync_irq: Trigger a VSYNC IRQ when the display is
 *                               updated.
 *
 * @s The DPDMA state.
 */
void xlnx_dpdma_trigger_vsync_irq(XlnxDPDMAState *s);

#endif /* XLNX_DPDMA_H */
