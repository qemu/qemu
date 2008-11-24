/*
 * QEMU internal VGA defines.
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#define MSR_COLOR_EMULATION 0x01
#define MSR_PAGE_SELECT     0x20

#define ST01_V_RETRACE      0x08
#define ST01_DISP_ENABLE    0x01

/* bochs VBE support */
#define CONFIG_BOCHS_VBE

#define VBE_DISPI_MAX_XRES              1600
#define VBE_DISPI_MAX_YRES              1200
#define VBE_DISPI_MAX_BPP               32

#define VBE_DISPI_INDEX_ID              0x0
#define VBE_DISPI_INDEX_XRES            0x1
#define VBE_DISPI_INDEX_YRES            0x2
#define VBE_DISPI_INDEX_BPP             0x3
#define VBE_DISPI_INDEX_ENABLE          0x4
#define VBE_DISPI_INDEX_BANK            0x5
#define VBE_DISPI_INDEX_VIRT_WIDTH      0x6
#define VBE_DISPI_INDEX_VIRT_HEIGHT     0x7
#define VBE_DISPI_INDEX_X_OFFSET        0x8
#define VBE_DISPI_INDEX_Y_OFFSET        0x9
#define VBE_DISPI_INDEX_NB              0xa

#define VBE_DISPI_ID0                   0xB0C0
#define VBE_DISPI_ID1                   0xB0C1
#define VBE_DISPI_ID2                   0xB0C2
#define VBE_DISPI_ID3                   0xB0C3
#define VBE_DISPI_ID4                   0xB0C4

#define VBE_DISPI_DISABLED              0x00
#define VBE_DISPI_ENABLED               0x01
#define VBE_DISPI_GETCAPS               0x02
#define VBE_DISPI_8BIT_DAC              0x20
#define VBE_DISPI_LFB_ENABLED           0x40
#define VBE_DISPI_NOCLEARMEM            0x80

#define VBE_DISPI_LFB_PHYSICAL_ADDRESS  0xE0000000

#ifdef CONFIG_BOCHS_VBE

#define VGA_STATE_COMMON_BOCHS_VBE              \
    uint16_t vbe_index;                         \
    uint16_t vbe_regs[VBE_DISPI_INDEX_NB];      \
    uint32_t vbe_start_addr;                    \
    uint32_t vbe_line_offset;                   \
    uint32_t vbe_bank_mask;

#else

#define VGA_STATE_COMMON_BOCHS_VBE

#endif /* !CONFIG_BOCHS_VBE */

#define CH_ATTR_SIZE (160 * 100)
#define VGA_MAX_HEIGHT 2048

struct vga_precise_retrace {
    int64_t ticks_per_char;
    int64_t total_chars;
    int htotal;
    int hstart;
    int hend;
    int vstart;
    int vend;
    int freq;
};

union vga_retrace {
    struct vga_precise_retrace precise;
};

struct VGAState;
typedef uint8_t (* vga_retrace_fn)(struct VGAState *s);
typedef void (* vga_update_retrace_info_fn)(struct VGAState *s);

#define VGA_STATE_COMMON                                                \
    uint8_t *vram_ptr;                                                  \
    ram_addr_t vram_offset;                                             \
    unsigned int vram_size;                                             \
    unsigned long bios_offset;                                          \
    unsigned int bios_size;                                             \
    target_phys_addr_t base_ctrl;                                       \
    int it_shift;                                                       \
    PCIDevice *pci_dev;                                                 \
    uint32_t latch;                                                     \
    uint8_t sr_index;                                                   \
    uint8_t sr[256];                                                    \
    uint8_t gr_index;                                                   \
    uint8_t gr[256];                                                    \
    uint8_t ar_index;                                                   \
    uint8_t ar[21];                                                     \
    int ar_flip_flop;                                                   \
    uint8_t cr_index;                                                   \
    uint8_t cr[256]; /* CRT registers */                                \
    uint8_t msr; /* Misc Output Register */                             \
    uint8_t fcr; /* Feature Control Register */                         \
    uint8_t st00; /* status 0 */                                        \
    uint8_t st01; /* status 1 */                                        \
    uint8_t dac_state;                                                  \
    uint8_t dac_sub_index;                                              \
    uint8_t dac_read_index;                                             \
    uint8_t dac_write_index;                                            \
    uint8_t dac_cache[3]; /* used when writing */                       \
    int dac_8bit;                                                       \
    uint8_t palette[768];                                               \
    int32_t bank_offset;                                                \
    int vga_io_memory;                                             \
    int (*get_bpp)(struct VGAState *s);                                 \
    void (*get_offsets)(struct VGAState *s,                             \
                        uint32_t *pline_offset,                         \
                        uint32_t *pstart_addr,                          \
                        uint32_t *pline_compare);                       \
    void (*get_resolution)(struct VGAState *s,                          \
                        int *pwidth,                                    \
                        int *pheight);                                  \
    VGA_STATE_COMMON_BOCHS_VBE                                          \
    /* display refresh support */                                       \
    DisplayState *ds;                                                   \
    QEMUConsole *console;                                               \
    uint32_t font_offsets[2];                                           \
    int graphic_mode;                                                   \
    uint8_t shift_control;                                              \
    uint8_t double_scan;                                                \
    uint32_t line_offset;                                               \
    uint32_t line_compare;                                              \
    uint32_t start_addr;                                                \
    uint32_t plane_updated;                                             \
    uint8_t last_cw, last_ch;                                           \
    uint32_t last_width, last_height; /* in chars or pixels */          \
    uint32_t last_scr_width, last_scr_height; /* in pixels */           \
    uint8_t cursor_start, cursor_end;                                   \
    uint32_t cursor_offset;                                             \
    unsigned int (*rgb_to_pixel)(unsigned int r,                        \
                                 unsigned int g, unsigned b);           \
    vga_hw_update_ptr update;                                           \
    vga_hw_invalidate_ptr invalidate;                                   \
    vga_hw_screen_dump_ptr screen_dump;                                 \
    vga_hw_text_update_ptr text_update;                                 \
    /* hardware mouse cursor support */                                 \
    uint32_t invalidated_y_table[VGA_MAX_HEIGHT / 32];                  \
    void (*cursor_invalidate)(struct VGAState *s);                      \
    void (*cursor_draw_line)(struct VGAState *s, uint8_t *d, int y);    \
    /* tell for each page if it has been updated since the last time */ \
    uint32_t last_palette[256];                                         \
    uint32_t last_ch_attr[CH_ATTR_SIZE]; /* XXX: make it dynamic */     \
    /* retrace */                                                       \
    vga_retrace_fn retrace;                                             \
    vga_update_retrace_info_fn update_retrace_info;                     \
    union vga_retrace retrace_info;


typedef struct VGAState {
    VGA_STATE_COMMON
} VGAState;

static inline int c6_to_8(int v)
{
    int b;
    v &= 0x3f;
    b = v & 1;
    return (v << 2) | (b << 1) | b;
}

void vga_common_init(VGAState *s, DisplayState *ds, uint8_t *vga_ram_base,
                     ram_addr_t vga_ram_offset, int vga_ram_size);
void vga_init(VGAState *s);
uint32_t vga_mem_readb(void *opaque, target_phys_addr_t addr);
void vga_mem_writeb(void *opaque, target_phys_addr_t addr, uint32_t val);
void vga_invalidate_scanlines(VGAState *s, int y1, int y2);
int ppm_save(const char *filename, uint8_t *data,
             int w, int h, int linesize);

void vga_draw_cursor_line_8(uint8_t *d1, const uint8_t *src1,
                            int poffset, int w,
                            unsigned int color0, unsigned int color1,
                            unsigned int color_xor);
void vga_draw_cursor_line_16(uint8_t *d1, const uint8_t *src1,
                             int poffset, int w,
                             unsigned int color0, unsigned int color1,
                             unsigned int color_xor);
void vga_draw_cursor_line_32(uint8_t *d1, const uint8_t *src1,
                             int poffset, int w,
                             unsigned int color0, unsigned int color1,
                             unsigned int color_xor);

extern const uint8_t sr_mask[8];
extern const uint8_t gr_mask[16];
