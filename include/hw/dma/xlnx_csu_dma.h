/*
 * Xilinx Platform CSU Stream DMA emulation
 *
 * This implementation is based on
 * https://github.com/Xilinx/qemu/blob/master/hw/dma/csu_stream_dma.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef XLNX_CSU_DMA_H
#define XLNX_CSU_DMA_H

#define TYPE_XLNX_CSU_DMA "xlnx.csu_dma"

#define XLNX_CSU_DMA_R_MAX (0x2c / 4)

typedef struct XlnxCSUDMA {
    SysBusDevice busdev;
    MemoryRegion iomem;
    MemTxAttrs attr;
    MemoryRegion *dma_mr;
    AddressSpace dma_as;
    qemu_irq irq;
    StreamSink *tx_dev; /* Used as generic StreamSink */
    ptimer_state *src_timer;

    uint16_t width;
    bool is_dst;
    bool r_size_last_word;

    StreamCanPushNotifyFn notify;
    void *notify_opaque;

    uint32_t regs[XLNX_CSU_DMA_R_MAX];
    RegisterInfo regs_info[XLNX_CSU_DMA_R_MAX];
} XlnxCSUDMA;

#define XLNX_CSU_DMA(obj) \
    OBJECT_CHECK(XlnxCSUDMA, (obj), TYPE_XLNX_CSU_DMA)

#endif
