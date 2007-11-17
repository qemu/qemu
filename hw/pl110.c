/*
 * Arm PrimeCell PL110 Color LCD Controller
 *
 * Copyright (c) 2005-2006 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licenced under the GNU LGPL
 */

#include "hw.h"
#include "primecell.h"
#include "console.h"

#define PL110_CR_EN   0x001
#define PL110_CR_BGR  0x100
#define PL110_CR_BEBO 0x200
#define PL110_CR_BEPO 0x400
#define PL110_CR_PWR  0x800

enum pl110_bppmode
{
    BPP_1,
    BPP_2,
    BPP_4,
    BPP_8,
    BPP_16,
    BPP_32
};

typedef struct {
    uint32_t base;
    DisplayState *ds;
    /* The Versatile/PB uses a slightly modified PL110 controller.  */
    int versatile;
    uint32_t timing[4];
    uint32_t cr;
    uint32_t upbase;
    uint32_t lpbase;
    uint32_t int_status;
    uint32_t int_mask;
    int cols;
    int rows;
    enum pl110_bppmode bpp;
    int invalidate;
    uint32_t pallette[256];
    uint32_t raw_pallette[128];
    qemu_irq irq;
} pl110_state;

static const unsigned char pl110_id[] =
{ 0x10, 0x11, 0x04, 0x00, 0x0d, 0xf0, 0x05, 0xb1 };

/* The Arm documentation (DDI0224C) says the CLDC on the Versatile board
   has a different ID.  However Linux only looks for the normal ID.  */
#if 0
static const unsigned char pl110_versatile_id[] =
{ 0x93, 0x10, 0x04, 0x00, 0x0d, 0xf0, 0x05, 0xb1 };
#else
#define pl110_versatile_id pl110_id
#endif

static inline uint32_t rgb_to_pixel8(unsigned int r, unsigned int g, unsigned b)
{
    return ((r >> 5) << 5) | ((g >> 5) << 2) | (b >> 6);
}

static inline uint32_t rgb_to_pixel15(unsigned int r, unsigned int g, unsigned b)
{
    return ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3);
}

static inline uint32_t rgb_to_pixel16(unsigned int r, unsigned int g, unsigned b)
{
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

static inline uint32_t rgb_to_pixel24(unsigned int r, unsigned int g, unsigned b)
{
    return (r << 16) | (g << 8) | b;
}

static inline uint32_t rgb_to_pixel32(unsigned int r, unsigned int g, unsigned b)
{
    return (r << 16) | (g << 8) | b;
}

typedef void (*drawfn)(uint32_t *, uint8_t *, const uint8_t *, int);

#define BITS 8
#include "pl110_template.h"
#define BITS 15
#include "pl110_template.h"
#define BITS 16
#include "pl110_template.h"
#define BITS 24
#include "pl110_template.h"
#define BITS 32
#include "pl110_template.h"

static int pl110_enabled(pl110_state *s)
{
  return (s->cr & PL110_CR_EN) && (s->cr & PL110_CR_PWR);
}

static void pl110_update_display(void *opaque)
{
    pl110_state *s = (pl110_state *)opaque;
    drawfn* fntable;
    drawfn fn;
    uint32_t *pallette;
    uint32_t addr;
    uint32_t base;
    int dest_width;
    int src_width;
    uint8_t *dest;
    uint8_t *src;
    int first, last = 0;
    int dirty, new_dirty;
    int i;
    int bpp_offset;

    if (!pl110_enabled(s))
        return;

    switch (s->ds->depth) {
    case 0:
        return;
    case 8:
        fntable = pl110_draw_fn_8;
        dest_width = 1;
        break;
    case 15:
        fntable = pl110_draw_fn_15;
        dest_width = 2;
        break;
    case 16:
        fntable = pl110_draw_fn_16;
        dest_width = 2;
        break;
    case 24:
        fntable = pl110_draw_fn_24;
        dest_width = 3;
        break;
    case 32:
        fntable = pl110_draw_fn_32;
        dest_width = 4;
        break;
    default:
        fprintf(stderr, "pl110: Bad color depth\n");
        exit(1);
    }
    if (s->cr & PL110_CR_BGR)
        bpp_offset = 0;
    else
        bpp_offset = 18;

    if (s->cr & PL110_CR_BEBO)
        fn = fntable[s->bpp + 6 + bpp_offset];
    else if (s->cr & PL110_CR_BEPO)
        fn = fntable[s->bpp + 12 + bpp_offset];
    else
        fn = fntable[s->bpp + bpp_offset];

    src_width = s->cols;
    switch (s->bpp) {
    case BPP_1:
        src_width >>= 3;
        break;
    case BPP_2:
        src_width >>= 2;
        break;
    case BPP_4:
        src_width >>= 1;
        break;
    case BPP_8:
        break;
    case BPP_16:
        src_width <<= 1;
        break;
    case BPP_32:
        src_width <<= 2;
        break;
    }
    dest_width *= s->cols;
    pallette = s->pallette;
    base = s->upbase;
    /* HACK: Arm aliases physical memory at 0x80000000.  */
    if (base > 0x80000000)
        base -= 0x80000000;
    src = phys_ram_base + base;
    dest = s->ds->data;
    first = -1;
    addr = base;

    dirty = cpu_physical_memory_get_dirty(addr, VGA_DIRTY_FLAG);
    new_dirty = dirty;
    for (i = 0; i < s->rows; i++) {
        if ((addr & ~TARGET_PAGE_MASK) + src_width >= TARGET_PAGE_SIZE) {
            uint32_t tmp;
            new_dirty = 0;
            for (tmp = 0; tmp < src_width; tmp += TARGET_PAGE_SIZE) {
                new_dirty |= cpu_physical_memory_get_dirty(addr + tmp,
                                                           VGA_DIRTY_FLAG);
            }
        }

        if (dirty || new_dirty || s->invalidate) {
            fn(pallette, dest, src, s->cols);
            if (first == -1)
                first = i;
            last = i;
        }
        dirty = new_dirty;
        addr += src_width;
        dest += dest_width;
        src += src_width;
    }
    if (first < 0)
      return;

    s->invalidate = 0;
    cpu_physical_memory_reset_dirty(base + first * src_width,
                                    base + (last + 1) * src_width,
                                    VGA_DIRTY_FLAG);
    dpy_update(s->ds, 0, first, s->cols, last - first + 1);
}

static void pl110_invalidate_display(void * opaque)
{
    pl110_state *s = (pl110_state *)opaque;
    s->invalidate = 1;
}

static void pl110_update_pallette(pl110_state *s, int n)
{
    int i;
    uint32_t raw;
    unsigned int r, g, b;

    raw = s->raw_pallette[n];
    n <<= 1;
    for (i = 0; i < 2; i++) {
        r = (raw & 0x1f) << 3;
        raw >>= 5;
        g = (raw & 0x1f) << 3;
        raw >>= 5;
        b = (raw & 0x1f) << 3;
        /* The I bit is ignored.  */
        raw >>= 6;
        switch (s->ds->depth) {
        case 8:
            s->pallette[n] = rgb_to_pixel8(r, g, b);
            break;
        case 15:
            s->pallette[n] = rgb_to_pixel15(r, g, b);
            break;
        case 16:
            s->pallette[n] = rgb_to_pixel16(r, g, b);
            break;
        case 24:
        case 32:
            s->pallette[n] = rgb_to_pixel32(r, g, b);
            break;
        }
        n++;
    }
}

static void pl110_resize(pl110_state *s, int width, int height)
{
    if (width != s->cols || height != s->rows) {
        if (pl110_enabled(s)) {
            dpy_resize(s->ds, width, height);
        }
    }
    s->cols = width;
    s->rows = height;
}

/* Update interrupts.  */
static void pl110_update(pl110_state *s)
{
  /* TODO: Implement interrupts.  */
}

static uint32_t pl110_read(void *opaque, target_phys_addr_t offset)
{
    pl110_state *s = (pl110_state *)opaque;

    offset -= s->base;
    if (offset >= 0xfe0 && offset < 0x1000) {
        if (s->versatile)
            return pl110_versatile_id[(offset - 0xfe0) >> 2];
        else
            return pl110_id[(offset - 0xfe0) >> 2];
    }
    if (offset >= 0x200 && offset < 0x400) {
        return s->raw_pallette[(offset - 0x200) >> 2];
    }
    switch (offset >> 2) {
    case 0: /* LCDTiming0 */
        return s->timing[0];
    case 1: /* LCDTiming1 */
        return s->timing[1];
    case 2: /* LCDTiming2 */
        return s->timing[2];
    case 3: /* LCDTiming3 */
        return s->timing[3];
    case 4: /* LCDUPBASE */
        return s->upbase;
    case 5: /* LCDLPBASE */
        return s->lpbase;
    case 6: /* LCDIMSC */
	if (s->versatile)
	  return s->cr;
        return s->int_mask;
    case 7: /* LCDControl */
	if (s->versatile)
	  return s->int_mask;
        return s->cr;
    case 8: /* LCDRIS */
        return s->int_status;
    case 9: /* LCDMIS */
        return s->int_status & s->int_mask;
    case 11: /* LCDUPCURR */
        /* TODO: Implement vertical refresh.  */
        return s->upbase;
    case 12: /* LCDLPCURR */
        return s->lpbase;
    default:
        cpu_abort (cpu_single_env, "pl110_read: Bad offset %x\n", (int)offset);
        return 0;
    }
}

static void pl110_write(void *opaque, target_phys_addr_t offset,
                        uint32_t val)
{
    pl110_state *s = (pl110_state *)opaque;
    int n;

    /* For simplicity invalidate the display whenever a control register
       is writen to.  */
    s->invalidate = 1;
    offset -= s->base;
    if (offset >= 0x200 && offset < 0x400) {
        /* Pallette.  */
        n = (offset - 0x200) >> 2;
        s->raw_pallette[(offset - 0x200) >> 2] = val;
        pl110_update_pallette(s, n);
        return;
    }
    switch (offset >> 2) {
    case 0: /* LCDTiming0 */
        s->timing[0] = val;
        n = ((val & 0xfc) + 4) * 4;
        pl110_resize(s, n, s->rows);
        break;
    case 1: /* LCDTiming1 */
        s->timing[1] = val;
        n = (val & 0x3ff) + 1;
        pl110_resize(s, s->cols, n);
        break;
    case 2: /* LCDTiming2 */
        s->timing[2] = val;
        break;
    case 3: /* LCDTiming3 */
        s->timing[3] = val;
        break;
    case 4: /* LCDUPBASE */
        s->upbase = val;
        break;
    case 5: /* LCDLPBASE */
        s->lpbase = val;
        break;
    case 6: /* LCDIMSC */
        if (s->versatile)
            goto control;
    imsc:
        s->int_mask = val;
        pl110_update(s);
        break;
    case 7: /* LCDControl */
        if (s->versatile)
            goto imsc;
    control:
        s->cr = val;
        s->bpp = (val >> 1) & 7;
        if (pl110_enabled(s)) {
            dpy_resize(s->ds, s->cols, s->rows);
        }
        break;
    case 10: /* LCDICR */
        s->int_status &= ~val;
        pl110_update(s);
        break;
    default:
        cpu_abort (cpu_single_env, "pl110_write: Bad offset %x\n", (int)offset);
    }
}

static CPUReadMemoryFunc *pl110_readfn[] = {
   pl110_read,
   pl110_read,
   pl110_read
};

static CPUWriteMemoryFunc *pl110_writefn[] = {
   pl110_write,
   pl110_write,
   pl110_write
};

void *pl110_init(DisplayState *ds, uint32_t base, qemu_irq irq,
                 int versatile)
{
    pl110_state *s;
    int iomemtype;

    s = (pl110_state *)qemu_mallocz(sizeof(pl110_state));
    iomemtype = cpu_register_io_memory(0, pl110_readfn,
                                       pl110_writefn, s);
    cpu_register_physical_memory(base, 0x00001000, iomemtype);
    s->base = base;
    s->ds = ds;
    s->versatile = versatile;
    s->irq = irq;
    graphic_console_init(ds, pl110_update_display, pl110_invalidate_display,
                         NULL, s);
    /* ??? Save/restore.  */
    return s;
}
