/*
 * QEMU ATI SVGA emulation
 *
 * Copyright (c) 2019 BALATON Zoltan
 *
 * This work is licensed under the GNU GPL license version 2 or later.
 */

/*
 * WARNING:
 * This is very incomplete and only enough for Linux console and some
 * unaccelerated X output at the moment.
 * Currently it's little more than a frame buffer with minimal functions,
 * other more advanced features of the hardware are yet to be implemented.
 * We only aim for Rage 128 Pro (and some RV100) and 2D only at first,
 * No 3D at all yet (maybe after 2D works, but feel free to improve it)
 */

#include "qemu/osdep.h"
#include "ati_int.h"
#include "ati_regs.h"
#include "vga_regs.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "ui/console.h"
#include "hw/display/i2c-ddc.h"
#include "trace.h"

#define ATI_DEBUG_HW_CURSOR 0

static const struct {
    const char *name;
    uint16_t dev_id;
} ati_model_aliases[] = {
    { "rage128p", PCI_DEVICE_ID_ATI_RAGE128_PF },
    { "rv100", PCI_DEVICE_ID_ATI_RADEON_QY },
};

enum { VGA_MODE, EXT_MODE };

static void ati_vga_switch_mode(ATIVGAState *s)
{
    DPRINTF("%d -> %d\n",
            s->mode, !!(s->regs.crtc_gen_cntl & CRTC2_EXT_DISP_EN));
    if (s->regs.crtc_gen_cntl & CRTC2_EXT_DISP_EN) {
        /* Extended mode enabled */
        s->mode = EXT_MODE;
        if (s->regs.crtc_gen_cntl & CRTC2_EN) {
            /* CRT controller enabled, use CRTC values */
            uint32_t offs = s->regs.crtc_offset & 0x07ffffff;
            int stride = (s->regs.crtc_pitch & 0x7ff) * 8;
            int bpp = 0;
            int h, v;

            if (s->regs.crtc_h_total_disp == 0) {
                s->regs.crtc_h_total_disp = ((640 / 8) - 1) << 16;
            }
            if (s->regs.crtc_v_total_disp == 0) {
                s->regs.crtc_v_total_disp = (480 - 1) << 16;
            }
            h = ((s->regs.crtc_h_total_disp >> 16) + 1) * 8;
            v = (s->regs.crtc_v_total_disp >> 16) + 1;
            switch (s->regs.crtc_gen_cntl & CRTC_PIX_WIDTH_MASK) {
            case CRTC_PIX_WIDTH_4BPP:
                bpp = 4;
                break;
            case CRTC_PIX_WIDTH_8BPP:
                bpp = 8;
                break;
            case CRTC_PIX_WIDTH_15BPP:
                bpp = 15;
                break;
            case CRTC_PIX_WIDTH_16BPP:
                bpp = 16;
                break;
            case CRTC_PIX_WIDTH_24BPP:
                bpp = 24;
                break;
            case CRTC_PIX_WIDTH_32BPP:
                bpp = 32;
                break;
            default:
                qemu_log_mask(LOG_UNIMP, "Unsupported bpp value\n");
            }
            assert(bpp != 0);
            DPRINTF("Switching to %dx%d %d %d @ %x\n", h, v, stride, bpp, offs);
            vbe_ioport_write_index(&s->vga, 0, VBE_DISPI_INDEX_ENABLE);
            vbe_ioport_write_data(&s->vga, 0, VBE_DISPI_DISABLED);
            /* reset VBE regs then set up mode */
            s->vga.vbe_regs[VBE_DISPI_INDEX_XRES] = h;
            s->vga.vbe_regs[VBE_DISPI_INDEX_YRES] = v;
            s->vga.vbe_regs[VBE_DISPI_INDEX_BPP] = bpp;
            /* enable mode via ioport so it updates vga regs */
            vbe_ioport_write_index(&s->vga, 0, VBE_DISPI_INDEX_ENABLE);
            vbe_ioport_write_data(&s->vga, 0, VBE_DISPI_ENABLED |
                VBE_DISPI_LFB_ENABLED | VBE_DISPI_NOCLEARMEM |
                (s->regs.dac_cntl & DAC_8BIT_EN ? VBE_DISPI_8BIT_DAC : 0));
            /* now set offset and stride after enable as that resets these */
            if (stride) {
                vbe_ioport_write_index(&s->vga, 0, VBE_DISPI_INDEX_VIRT_WIDTH);
                vbe_ioport_write_data(&s->vga, 0, stride);
                if (offs % stride == 0) {
                    vbe_ioport_write_index(&s->vga, 0, VBE_DISPI_INDEX_Y_OFFSET);
                    vbe_ioport_write_data(&s->vga, 0, offs / stride);
                } else {
                    /* FIXME what to do with this? */
                    error_report("VGA offset is not multiple of pitch, "
                                 "expect bad picture");
                }
            }
        }
    } else {
        /* VGA mode enabled */
        s->mode = VGA_MODE;
        vbe_ioport_write_index(&s->vga, 0, VBE_DISPI_INDEX_ENABLE);
        vbe_ioport_write_data(&s->vga, 0, VBE_DISPI_DISABLED);
    }
}

/* Used by host side hardware cursor */
static void ati_cursor_define(ATIVGAState *s)
{
    uint8_t data[1024];
    uint8_t *src;
    int i, j, idx = 0;

    if ((s->regs.cur_offset & BIT(31)) || s->cursor_guest_mode) {
        return; /* Do not update cursor if locked or rendered by guest */
    }
    /* FIXME handle cur_hv_offs correctly */
    src = s->vga.vram_ptr + (s->regs.crtc_offset & 0x07ffffff) +
          s->regs.cur_offset - (s->regs.cur_hv_offs >> 16) -
          (s->regs.cur_hv_offs & 0xffff) * 16;
    for (i = 0; i < 64; i++) {
        for (j = 0; j < 8; j++, idx++) {
            data[idx] = src[i * 16 + j];
            data[512 + idx] = src[i * 16 + j + 8];
        }
    }
    if (!s->cursor) {
        s->cursor = cursor_alloc(64, 64);
    }
    cursor_set_mono(s->cursor, s->regs.cur_color1, s->regs.cur_color0,
                    &data[512], 1, &data[0]);
    dpy_cursor_define(s->vga.con, s->cursor);
}

/* Alternatively support guest rendered hardware cursor */
static void ati_cursor_invalidate(VGACommonState *vga)
{
    ATIVGAState *s = container_of(vga, ATIVGAState, vga);
    int size = (s->regs.crtc_gen_cntl & CRTC2_CUR_EN) ? 64 : 0;

    if (s->regs.cur_offset & BIT(31)) {
        return; /* Do not update cursor if locked */
    }
    if (s->cursor_size != size ||
        vga->hw_cursor_x != s->regs.cur_hv_pos >> 16 ||
        vga->hw_cursor_y != (s->regs.cur_hv_pos & 0xffff) ||
        s->cursor_offset != s->regs.cur_offset - (s->regs.cur_hv_offs >> 16) -
        (s->regs.cur_hv_offs & 0xffff) * 16) {
        /* Remove old cursor then update and show new one if needed */
        vga_invalidate_scanlines(vga, vga->hw_cursor_y, vga->hw_cursor_y + 63);
        vga->hw_cursor_x = s->regs.cur_hv_pos >> 16;
        vga->hw_cursor_y = s->regs.cur_hv_pos & 0xffff;
        s->cursor_offset = s->regs.cur_offset - (s->regs.cur_hv_offs >> 16) -
                           (s->regs.cur_hv_offs & 0xffff) * 16;
        s->cursor_size = size;
        if (size) {
            vga_invalidate_scanlines(vga,
                                     vga->hw_cursor_y, vga->hw_cursor_y + 63);
        }
    }
}

static void ati_cursor_draw_line(VGACommonState *vga, uint8_t *d, int scr_y)
{
    ATIVGAState *s = container_of(vga, ATIVGAState, vga);
    uint8_t *src;
    uint32_t *dp = (uint32_t *)d;
    int i, j, h;

    if (!(s->regs.crtc_gen_cntl & CRTC2_CUR_EN) ||
        scr_y < vga->hw_cursor_y || scr_y >= vga->hw_cursor_y + 64 ||
        scr_y > s->regs.crtc_v_total_disp >> 16) {
        return;
    }
    /* FIXME handle cur_hv_offs correctly */
    src = s->vga.vram_ptr + (s->regs.crtc_offset & 0x07ffffff) +
          s->cursor_offset + (scr_y - vga->hw_cursor_y) * 16;
    dp = &dp[vga->hw_cursor_x];
    h = ((s->regs.crtc_h_total_disp >> 16) + 1) * 8;
    for (i = 0; i < 8; i++) {
        uint32_t color;
        uint8_t abits = src[i];
        uint8_t xbits = src[i + 8];
        for (j = 0; j < 8; j++, abits <<= 1, xbits <<= 1) {
            if (abits & BIT(7)) {
                if (xbits & BIT(7)) {
                    color = dp[i * 8 + j] ^ 0xffffffff; /* complement */
                } else {
                    continue; /* transparent, no change */
                }
            } else {
                color = (xbits & BIT(7) ? s->regs.cur_color1 :
                                          s->regs.cur_color0) << 8 | 0xff;
            }
            if (vga->hw_cursor_x + i * 8 + j >= h) {
                return; /* end of screen, don't span to next line */
            }
            dp[i * 8 + j] = color;
        }
    }
}

static uint64_t ati_i2c(bitbang_i2c_interface *i2c, uint64_t data, int base)
{
    bool c = (data & BIT(base + 17) ? !!(data & BIT(base + 1)) : 1);
    bool d = (data & BIT(base + 16) ? !!(data & BIT(base)) : 1);

    bitbang_i2c_set(i2c, BITBANG_I2C_SCL, c);
    d = bitbang_i2c_set(i2c, BITBANG_I2C_SDA, d);

    data &= ~0xf00ULL;
    if (c) {
        data |= BIT(base + 9);
    }
    if (d) {
        data |= BIT(base + 8);
    }
    return data;
}

static inline uint64_t ati_reg_read_offs(uint32_t reg, int offs,
                                         unsigned int size)
{
    if (offs == 0 && size == 4) {
        return reg;
    } else {
        return extract32(reg, offs * BITS_PER_BYTE, size * BITS_PER_BYTE);
    }
}

static uint64_t ati_mm_read(void *opaque, hwaddr addr, unsigned int size)
{
    ATIVGAState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case MM_INDEX:
        val = s->regs.mm_index;
        break;
    case MM_DATA ... MM_DATA + 3:
        /* indexed access to regs or memory */
        if (s->regs.mm_index & BIT(31)) {
            uint32_t idx = s->regs.mm_index & ~BIT(31);
            if (idx <= s->vga.vram_size - size) {
                val = ldn_le_p(s->vga.vram_ptr + idx, size);
            }
        } else {
            val = ati_mm_read(s, s->regs.mm_index + addr - MM_DATA, size);
        }
        break;
    case BIOS_0_SCRATCH ... BUS_CNTL - 1:
    {
        int i = (addr - BIOS_0_SCRATCH) / 4;
        if (s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF && i > 3) {
            break;
        }
        val = ati_reg_read_offs(s->regs.bios_scratch[i],
                                addr - (BIOS_0_SCRATCH + i * 4), size);
        break;
    }
    case CRTC_GEN_CNTL ... CRTC_GEN_CNTL + 3:
        val = ati_reg_read_offs(s->regs.crtc_gen_cntl,
                                addr - CRTC_GEN_CNTL, size);
        break;
    case CRTC_EXT_CNTL ... CRTC_EXT_CNTL + 3:
        val = ati_reg_read_offs(s->regs.crtc_ext_cntl,
                                addr - CRTC_EXT_CNTL, size);
        break;
    case DAC_CNTL:
        val = s->regs.dac_cntl;
        break;
    case GPIO_VGA_DDC:
        val = s->regs.gpio_vga_ddc;
        break;
    case GPIO_DVI_DDC:
        val = s->regs.gpio_dvi_ddc;
        break;
    case GPIO_MONID ... GPIO_MONID + 3:
        val = ati_reg_read_offs(s->regs.gpio_monid,
                                addr - GPIO_MONID, size);
        break;
    case PALETTE_INDEX:
        /* FIXME unaligned access */
        val = vga_ioport_read(&s->vga, VGA_PEL_IR) << 16;
        val |= vga_ioport_read(&s->vga, VGA_PEL_IW) & 0xff;
        break;
    case PALETTE_DATA:
        val = vga_ioport_read(&s->vga, VGA_PEL_D);
        break;
    case CNFG_MEMSIZE:
        val = s->vga.vram_size;
        break;
    case MC_STATUS:
        val = 5;
        break;
    case RBBM_STATUS:
    case GUI_STAT:
        val = 64; /* free CMDFIFO entries */
        break;
    case CRTC_H_TOTAL_DISP:
        val = s->regs.crtc_h_total_disp;
        break;
    case CRTC_H_SYNC_STRT_WID:
        val = s->regs.crtc_h_sync_strt_wid;
        break;
    case CRTC_V_TOTAL_DISP:
        val = s->regs.crtc_v_total_disp;
        break;
    case CRTC_V_SYNC_STRT_WID:
        val = s->regs.crtc_v_sync_strt_wid;
        break;
    case CRTC_OFFSET:
        val = s->regs.crtc_offset;
        break;
    case CRTC_OFFSET_CNTL:
        val = s->regs.crtc_offset_cntl;
        break;
    case CRTC_PITCH:
        val = s->regs.crtc_pitch;
        break;
    case 0xf00 ... 0xfff:
        val = pci_default_read_config(&s->dev, addr - 0xf00, size);
        break;
    case CUR_OFFSET:
        val = s->regs.cur_offset;
        break;
    case CUR_HORZ_VERT_POSN:
        val = s->regs.cur_hv_pos;
        val |= s->regs.cur_offset & BIT(31);
        break;
    case CUR_HORZ_VERT_OFF:
        val = s->regs.cur_hv_offs;
        val |= s->regs.cur_offset & BIT(31);
        break;
    case CUR_CLR0:
        val = s->regs.cur_color0;
        break;
    case CUR_CLR1:
        val = s->regs.cur_color1;
        break;
    case DST_OFFSET:
        val = s->regs.dst_offset;
        break;
    case DST_PITCH:
        val = s->regs.dst_pitch;
        if (s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF) {
            val &= s->regs.dst_tile << 16;
        }
        break;
    case DST_WIDTH:
        val = s->regs.dst_width;
        break;
    case DST_HEIGHT:
        val = s->regs.dst_height;
        break;
    case SRC_X:
        val = s->regs.src_x;
        break;
    case SRC_Y:
        val = s->regs.src_y;
        break;
    case DST_X:
        val = s->regs.dst_x;
        break;
    case DST_Y:
        val = s->regs.dst_y;
        break;
    case DP_GUI_MASTER_CNTL:
        val = s->regs.dp_gui_master_cntl;
        break;
    case SRC_OFFSET:
        val = s->regs.src_offset;
        break;
    case SRC_PITCH:
        val = s->regs.src_pitch;
        if (s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF) {
            val &= s->regs.src_tile << 16;
        }
        break;
    case DP_BRUSH_BKGD_CLR:
        val = s->regs.dp_brush_bkgd_clr;
        break;
    case DP_BRUSH_FRGD_CLR:
        val = s->regs.dp_brush_frgd_clr;
        break;
    case DP_SRC_FRGD_CLR:
        val = s->regs.dp_src_frgd_clr;
        break;
    case DP_SRC_BKGD_CLR:
        val = s->regs.dp_src_bkgd_clr;
        break;
    case DP_CNTL:
        val = s->regs.dp_cntl;
        break;
    case DP_DATATYPE:
        val = s->regs.dp_datatype;
        break;
    case DP_MIX:
        val = s->regs.dp_mix;
        break;
    case DP_WRITE_MASK:
        val = s->regs.dp_write_mask;
        break;
    case DEFAULT_OFFSET:
        val = s->regs.default_offset;
        if (s->dev_id != PCI_DEVICE_ID_ATI_RAGE128_PF) {
            val >>= 10;
            val |= s->regs.default_pitch << 16;
            val |= s->regs.default_tile << 30;
        }
        break;
    case DEFAULT_PITCH:
        val = s->regs.default_pitch;
        val |= s->regs.default_tile << 16;
        break;
    case DEFAULT_SC_BOTTOM_RIGHT:
        val = s->regs.default_sc_bottom_right;
        break;
    default:
        break;
    }
    if (addr < CUR_OFFSET || addr > CUR_CLR1 || ATI_DEBUG_HW_CURSOR) {
        trace_ati_mm_read(size, addr, ati_reg_name(addr & ~3ULL), val);
    }
    return val;
}

static inline void ati_reg_write_offs(uint32_t *reg, int offs,
                                      uint64_t data, unsigned int size)
{
    if (offs == 0 && size == 4) {
        *reg = data;
    } else {
        *reg = deposit32(*reg, offs * BITS_PER_BYTE, size * BITS_PER_BYTE,
                         data);
    }
}

static void ati_mm_write(void *opaque, hwaddr addr,
                           uint64_t data, unsigned int size)
{
    ATIVGAState *s = opaque;

    if (addr < CUR_OFFSET || addr > CUR_CLR1 || ATI_DEBUG_HW_CURSOR) {
        trace_ati_mm_write(size, addr, ati_reg_name(addr & ~3ULL), data);
    }
    switch (addr) {
    case MM_INDEX:
        s->regs.mm_index = data;
        break;
    case MM_DATA ... MM_DATA + 3:
        /* indexed access to regs or memory */
        if (s->regs.mm_index & BIT(31)) {
            uint32_t idx = s->regs.mm_index & ~BIT(31);
            if (idx <= s->vga.vram_size - size) {
                stn_le_p(s->vga.vram_ptr + idx, size, data);
            }
        } else {
            ati_mm_write(s, s->regs.mm_index + addr - MM_DATA, data, size);
        }
        break;
    case BIOS_0_SCRATCH ... BUS_CNTL - 1:
    {
        int i = (addr - BIOS_0_SCRATCH) / 4;
        if (s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF && i > 3) {
            break;
        }
        ati_reg_write_offs(&s->regs.bios_scratch[i],
                           addr - (BIOS_0_SCRATCH + i * 4), data, size);
        break;
    }
    case CRTC_GEN_CNTL ... CRTC_GEN_CNTL + 3:
    {
        uint32_t val = s->regs.crtc_gen_cntl;
        ati_reg_write_offs(&s->regs.crtc_gen_cntl,
                           addr - CRTC_GEN_CNTL, data, size);
        if ((val & CRTC2_CUR_EN) != (s->regs.crtc_gen_cntl & CRTC2_CUR_EN)) {
            if (s->cursor_guest_mode) {
                s->vga.force_shadow = !!(s->regs.crtc_gen_cntl & CRTC2_CUR_EN);
            } else {
                if (s->regs.crtc_gen_cntl & CRTC2_CUR_EN) {
                    ati_cursor_define(s);
                }
                dpy_mouse_set(s->vga.con, s->regs.cur_hv_pos >> 16,
                              s->regs.cur_hv_pos & 0xffff,
                              (s->regs.crtc_gen_cntl & CRTC2_CUR_EN) != 0);
            }
        }
        if ((val & (CRTC2_EXT_DISP_EN | CRTC2_EN)) !=
            (s->regs.crtc_gen_cntl & (CRTC2_EXT_DISP_EN | CRTC2_EN))) {
            ati_vga_switch_mode(s);
        }
        break;
    }
    case CRTC_EXT_CNTL ... CRTC_EXT_CNTL + 3:
    {
        uint32_t val = s->regs.crtc_ext_cntl;
        ati_reg_write_offs(&s->regs.crtc_ext_cntl,
                           addr - CRTC_EXT_CNTL, data, size);
        if (s->regs.crtc_ext_cntl & CRT_CRTC_DISPLAY_DIS) {
            DPRINTF("Display disabled\n");
            s->vga.ar_index &= ~BIT(5);
        } else {
            DPRINTF("Display enabled\n");
            s->vga.ar_index |= BIT(5);
            ati_vga_switch_mode(s);
        }
        if ((val & CRT_CRTC_DISPLAY_DIS) !=
            (s->regs.crtc_ext_cntl & CRT_CRTC_DISPLAY_DIS)) {
            ati_vga_switch_mode(s);
        }
        break;
    }
    case DAC_CNTL:
        s->regs.dac_cntl = data & 0xffffe3ff;
        s->vga.dac_8bit = !!(data & DAC_8BIT_EN);
        break;
    case GPIO_VGA_DDC:
        if (s->dev_id != PCI_DEVICE_ID_ATI_RAGE128_PF) {
            /* FIXME: Maybe add a property to select VGA or DVI port? */
        }
        break;
    case GPIO_DVI_DDC:
        if (s->dev_id != PCI_DEVICE_ID_ATI_RAGE128_PF) {
            s->regs.gpio_dvi_ddc = ati_i2c(&s->bbi2c, data, 0);
        }
        break;
    case GPIO_MONID ... GPIO_MONID + 3:
        /* FIXME What does Radeon have here? */
        if (s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF) {
            ati_reg_write_offs(&s->regs.gpio_monid,
                               addr - GPIO_MONID, data, size);
            /*
             * Rage128p accesses DDC used to get EDID via these bits.
             * Only touch i2c when write overlaps 3rd byte because some
             * drivers access this reg via multiple partial writes and
             * without this spurious bits would be sent.
             */
            if ((s->regs.gpio_monid & BIT(25)) &&
                addr <= GPIO_MONID + 2 && addr + size > GPIO_MONID + 2) {
                s->regs.gpio_monid = ati_i2c(&s->bbi2c, s->regs.gpio_monid, 1);
            }
        }
        break;
    case PALETTE_INDEX ... PALETTE_INDEX + 3:
        if (size == 4) {
            vga_ioport_write(&s->vga, VGA_PEL_IR, (data >> 16) & 0xff);
            vga_ioport_write(&s->vga, VGA_PEL_IW, data & 0xff);
        } else {
            if (addr == PALETTE_INDEX) {
                vga_ioport_write(&s->vga, VGA_PEL_IW, data & 0xff);
            } else {
                vga_ioport_write(&s->vga, VGA_PEL_IR, data & 0xff);
            }
        }
        break;
    case PALETTE_DATA ... PALETTE_DATA + 3:
        data <<= addr - PALETTE_DATA;
        data = bswap32(data) >> 8;
        vga_ioport_write(&s->vga, VGA_PEL_D, data & 0xff);
        data >>= 8;
        vga_ioport_write(&s->vga, VGA_PEL_D, data & 0xff);
        data >>= 8;
        vga_ioport_write(&s->vga, VGA_PEL_D, data & 0xff);
        break;
    case CRTC_H_TOTAL_DISP:
        s->regs.crtc_h_total_disp = data & 0x07ff07ff;
        break;
    case CRTC_H_SYNC_STRT_WID:
        s->regs.crtc_h_sync_strt_wid = data & 0x17bf1fff;
        break;
    case CRTC_V_TOTAL_DISP:
        s->regs.crtc_v_total_disp = data & 0x0fff0fff;
        break;
    case CRTC_V_SYNC_STRT_WID:
        s->regs.crtc_v_sync_strt_wid = data & 0x9f0fff;
        break;
    case CRTC_OFFSET:
        s->regs.crtc_offset = data & 0xc7ffffff;
        break;
    case CRTC_OFFSET_CNTL:
        s->regs.crtc_offset_cntl = data; /* FIXME */
        break;
    case CRTC_PITCH:
        s->regs.crtc_pitch = data & 0x07ff07ff;
        break;
    case 0xf00 ... 0xfff:
        /* read-only copy of PCI config space so ignore writes */
        break;
    case CUR_OFFSET:
        if (s->regs.cur_offset != (data & 0x87fffff0)) {
            s->regs.cur_offset = data & 0x87fffff0;
            ati_cursor_define(s);
        }
        break;
    case CUR_HORZ_VERT_POSN:
        s->regs.cur_hv_pos = data & 0x3fff0fff;
        if (data & BIT(31)) {
            s->regs.cur_offset |= data & BIT(31);
        } else if (s->regs.cur_offset & BIT(31)) {
            s->regs.cur_offset &= ~BIT(31);
            ati_cursor_define(s);
        }
        if (!s->cursor_guest_mode &&
            (s->regs.crtc_gen_cntl & CRTC2_CUR_EN) && !(data & BIT(31))) {
            dpy_mouse_set(s->vga.con, s->regs.cur_hv_pos >> 16,
                          s->regs.cur_hv_pos & 0xffff, 1);
        }
        break;
    case CUR_HORZ_VERT_OFF:
        s->regs.cur_hv_offs = data & 0x3f003f;
        if (data & BIT(31)) {
            s->regs.cur_offset |= data & BIT(31);
        } else if (s->regs.cur_offset & BIT(31)) {
            s->regs.cur_offset &= ~BIT(31);
            ati_cursor_define(s);
        }
        break;
    case CUR_CLR0:
        if (s->regs.cur_color0 != (data & 0xffffff)) {
            s->regs.cur_color0 = data & 0xffffff;
            ati_cursor_define(s);
        }
        break;
    case CUR_CLR1:
        /*
         * Update cursor unconditionally here because some clients set up
         * other registers before actually writing cursor data to memory at
         * offset so we would miss cursor change unless always updating here
         */
        s->regs.cur_color1 = data & 0xffffff;
        ati_cursor_define(s);
        break;
    case DST_OFFSET:
        if (s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF) {
            s->regs.dst_offset = data & 0xfffffff0;
        } else {
            s->regs.dst_offset = data & 0xfffffc00;
        }
        break;
    case DST_PITCH:
        if (s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF) {
            s->regs.dst_pitch = data & 0x3fff;
            s->regs.dst_tile = (data >> 16) & 1;
        } else {
            s->regs.dst_pitch = data & 0x3ff0;
        }
        break;
    case DST_TILE:
        if (s->dev_id == PCI_DEVICE_ID_ATI_RADEON_QY) {
            s->regs.dst_tile = data & 3;
        }
        break;
    case DST_WIDTH:
        s->regs.dst_width = data & 0x3fff;
        ati_2d_blt(s);
        break;
    case DST_HEIGHT:
        s->regs.dst_height = data & 0x3fff;
        break;
    case SRC_X:
        s->regs.src_x = data & 0x3fff;
        break;
    case SRC_Y:
        s->regs.src_y = data & 0x3fff;
        break;
    case DST_X:
        s->regs.dst_x = data & 0x3fff;
        break;
    case DST_Y:
        s->regs.dst_y = data & 0x3fff;
        break;
    case SRC_PITCH_OFFSET:
        if (s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF) {
            s->regs.src_offset = (data & 0x1fffff) << 4;
            s->regs.src_pitch = (data & 0x7fe00000) >> 21;
            s->regs.src_tile = data >> 31;
        } else {
            s->regs.src_offset = (data & 0x3fffff) << 10;
            s->regs.src_pitch = (data & 0x3fc00000) >> 16;
            s->regs.src_tile = (data >> 30) & 1;
        }
        break;
    case DST_PITCH_OFFSET:
        if (s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF) {
            s->regs.dst_offset = (data & 0x1fffff) << 4;
            s->regs.dst_pitch = (data & 0x7fe00000) >> 21;
            s->regs.dst_tile = data >> 31;
        } else {
            s->regs.dst_offset = (data & 0x3fffff) << 10;
            s->regs.dst_pitch = (data & 0x3fc00000) >> 16;
            s->regs.dst_tile = data >> 30;
        }
        break;
    case SRC_Y_X:
        s->regs.src_x = data & 0x3fff;
        s->regs.src_y = (data >> 16) & 0x3fff;
        break;
    case DST_Y_X:
        s->regs.dst_x = data & 0x3fff;
        s->regs.dst_y = (data >> 16) & 0x3fff;
        break;
    case DST_HEIGHT_WIDTH:
        s->regs.dst_width = data & 0x3fff;
        s->regs.dst_height = (data >> 16) & 0x3fff;
        ati_2d_blt(s);
        break;
    case DP_GUI_MASTER_CNTL:
        s->regs.dp_gui_master_cntl = data & 0xf800000f;
        s->regs.dp_datatype = (data & 0x0f00) >> 8 | (data & 0x30f0) << 4 |
                              (data & 0x4000) << 16;
        s->regs.dp_mix = (data & GMC_ROP3_MASK) | (data & 0x7000000) >> 16;
        break;
    case DST_WIDTH_X:
        s->regs.dst_x = data & 0x3fff;
        s->regs.dst_width = (data >> 16) & 0x3fff;
        ati_2d_blt(s);
        break;
    case SRC_X_Y:
        s->regs.src_y = data & 0x3fff;
        s->regs.src_x = (data >> 16) & 0x3fff;
        break;
    case DST_X_Y:
        s->regs.dst_y = data & 0x3fff;
        s->regs.dst_x = (data >> 16) & 0x3fff;
        break;
    case DST_WIDTH_HEIGHT:
        s->regs.dst_height = data & 0x3fff;
        s->regs.dst_width = (data >> 16) & 0x3fff;
        ati_2d_blt(s);
        break;
    case DST_HEIGHT_Y:
        s->regs.dst_y = data & 0x3fff;
        s->regs.dst_height = (data >> 16) & 0x3fff;
        break;
    case SRC_OFFSET:
        if (s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF) {
            s->regs.src_offset = data & 0xfffffff0;
        } else {
            s->regs.src_offset = data & 0xfffffc00;
        }
        break;
    case SRC_PITCH:
        if (s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF) {
            s->regs.src_pitch = data & 0x3fff;
            s->regs.src_tile = (data >> 16) & 1;
        } else {
            s->regs.src_pitch = data & 0x3ff0;
        }
        break;
    case DP_BRUSH_BKGD_CLR:
        s->regs.dp_brush_bkgd_clr = data;
        break;
    case DP_BRUSH_FRGD_CLR:
        s->regs.dp_brush_frgd_clr = data;
        break;
    case DP_CNTL:
        s->regs.dp_cntl = data;
        break;
    case DP_DATATYPE:
        s->regs.dp_datatype = data & 0xe0070f0f;
        break;
    case DP_MIX:
        s->regs.dp_mix = data & 0x00ff0700;
        break;
    case DP_WRITE_MASK:
        s->regs.dp_write_mask = data;
        break;
    case DEFAULT_OFFSET:
        if (s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF) {
            s->regs.default_offset = data & 0xfffffff0;
        } else {
            /* Radeon has DEFAULT_PITCH_OFFSET here like DST_PITCH_OFFSET */
            s->regs.default_offset = (data & 0x3fffff) << 10;
            s->regs.default_pitch = (data & 0x3fc00000) >> 16;
            s->regs.default_tile = data >> 30;
        }
        break;
    case DEFAULT_PITCH:
        if (s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF) {
            s->regs.default_pitch = data & 0x3fff;
            s->regs.default_tile = (data >> 16) & 1;
        }
        break;
    case DEFAULT_SC_BOTTOM_RIGHT:
        s->regs.default_sc_bottom_right = data & 0x3fff3fff;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps ati_mm_ops = {
    .read = ati_mm_read,
    .write = ati_mm_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void ati_vga_realize(PCIDevice *dev, Error **errp)
{
    ATIVGAState *s = ATI_VGA(dev);
    VGACommonState *vga = &s->vga;

    if (s->model) {
        int i;
        for (i = 0; i < ARRAY_SIZE(ati_model_aliases); i++) {
            if (!strcmp(s->model, ati_model_aliases[i].name)) {
                s->dev_id = ati_model_aliases[i].dev_id;
                break;
            }
        }
        if (i >= ARRAY_SIZE(ati_model_aliases)) {
            warn_report("Unknown ATI VGA model name, "
                        "using default rage128p");
        }
    }
    if (s->dev_id != PCI_DEVICE_ID_ATI_RAGE128_PF &&
        s->dev_id != PCI_DEVICE_ID_ATI_RADEON_QY) {
        error_setg(errp, "Unknown ATI VGA device id, "
                   "only 0x5046 and 0x5159 are supported");
        return;
    }
    pci_set_word(dev->config + PCI_DEVICE_ID, s->dev_id);

    if (s->dev_id == PCI_DEVICE_ID_ATI_RADEON_QY &&
        s->vga.vram_size_mb < 16) {
        warn_report("Too small video memory for device id");
        s->vga.vram_size_mb = 16;
    }

    /* init vga bits */
    vga_common_init(vga, OBJECT(s));
    vga_init(vga, OBJECT(s), pci_address_space(dev),
             pci_address_space_io(dev), true);
    vga->con = graphic_console_init(DEVICE(s), 0, s->vga.hw_ops, &s->vga);
    if (s->cursor_guest_mode) {
        vga->cursor_invalidate = ati_cursor_invalidate;
        vga->cursor_draw_line = ati_cursor_draw_line;
    }

    /* ddc, edid */
    I2CBus *i2cbus = i2c_init_bus(DEVICE(s), "ati-vga.ddc");
    bitbang_i2c_init(&s->bbi2c, i2cbus);
    I2CSlave *i2cddc = I2C_SLAVE(qdev_create(BUS(i2cbus), TYPE_I2CDDC));
    i2c_set_slave_address(i2cddc, 0x50);

    /* mmio register space */
    memory_region_init_io(&s->mm, OBJECT(s), &ati_mm_ops, s,
                          "ati.mmregs", 0x4000);
    /* io space is alias to beginning of mmregs */
    memory_region_init_alias(&s->io, OBJECT(s), "ati.io", &s->mm, 0, 0x100);

    pci_register_bar(dev, 0, PCI_BASE_ADDRESS_MEM_PREFETCH, &vga->vram);
    pci_register_bar(dev, 1, PCI_BASE_ADDRESS_SPACE_IO, &s->io);
    pci_register_bar(dev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mm);
}

static void ati_vga_reset(DeviceState *dev)
{
    ATIVGAState *s = ATI_VGA(dev);

    /* reset vga */
    vga_common_reset(&s->vga);
    s->mode = VGA_MODE;
}

static void ati_vga_exit(PCIDevice *dev)
{
    ATIVGAState *s = ATI_VGA(dev);

    graphic_console_close(s->vga.con);
}

static Property ati_vga_properties[] = {
    DEFINE_PROP_UINT32("vgamem_mb", ATIVGAState, vga.vram_size_mb, 16),
    DEFINE_PROP_STRING("model", ATIVGAState, model),
    DEFINE_PROP_UINT16("x-device-id", ATIVGAState, dev_id,
                       PCI_DEVICE_ID_ATI_RAGE128_PF),
    DEFINE_PROP_BOOL("guest_hwcursor", ATIVGAState, cursor_guest_mode, false),
    DEFINE_PROP_END_OF_LIST()
};

static void ati_vga_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    dc->reset = ati_vga_reset;
    dc->props = ati_vga_properties;
    dc->hotpluggable = false;
    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);

    k->class_id = PCI_CLASS_DISPLAY_VGA;
    k->vendor_id = PCI_VENDOR_ID_ATI;
    k->device_id = PCI_DEVICE_ID_ATI_RAGE128_PF;
    k->romfile = "vgabios-ati.bin";
    k->realize = ati_vga_realize;
    k->exit = ati_vga_exit;
}

static const TypeInfo ati_vga_info = {
    .name = TYPE_ATI_VGA,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(ATIVGAState),
    .class_init = ati_vga_class_init,
    .interfaces = (InterfaceInfo[]) {
          { INTERFACE_CONVENTIONAL_PCI_DEVICE },
          { },
    },
};

static void ati_vga_register_types(void)
{
    type_register_static(&ati_vga_info);
}

type_init(ati_vga_register_types)
