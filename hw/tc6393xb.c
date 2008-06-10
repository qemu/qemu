/*
 * Toshiba TC6393XB I/O Controller.
 * Found in Sharp Zaurus SL-6000 (tosa) or some
 * Toshiba e-Series PDAs.
 *
 * Most features are currently unsupported!!!
 *
 * This code is licensed under the GNU GPL v2.
 */
#include "hw.h"
#include "pxa.h"
#include "devices.h"

#define TC6393XB_GPIOS  16

#define SCR_REVID	0x08		/* b Revision ID	*/
#define SCR_ISR		0x50		/* b Interrupt Status	*/
#define SCR_IMR		0x52		/* b Interrupt Mask	*/
#define SCR_IRR		0x54		/* b Interrupt Routing	*/
#define SCR_GPER	0x60		/* w GP Enable		*/
#define SCR_GPI_SR(i)	(0x64 + (i))	/* b3 GPI Status	*/
#define SCR_GPI_IMR(i)	(0x68 + (i))	/* b3 GPI INT Mask	*/
#define SCR_GPI_EDER(i)	(0x6c + (i))	/* b3 GPI Edge Detect Enable */
#define SCR_GPI_LIR(i)	(0x70 + (i))	/* b3 GPI Level Invert	*/
#define SCR_GPO_DSR(i)	(0x78 + (i))	/* b3 GPO Data Set	*/
#define SCR_GPO_DOECR(i) (0x7c + (i))	/* b3 GPO Data OE Control */
#define SCR_GP_IARCR(i)	(0x80 + (i))	/* b3 GP Internal Active Register Control */
#define SCR_GP_IARLCR(i) (0x84 + (i))	/* b3 GP INTERNAL Active Register Level Control */
#define SCR_GPI_BCR(i)	(0x88 + (i))	/* b3 GPI Buffer Control */
#define SCR_GPA_IARCR	0x8c		/* w GPa Internal Active Register Control */
#define SCR_GPA_IARLCR	0x90		/* w GPa Internal Active Register Level Control */
#define SCR_GPA_BCR	0x94		/* w GPa Buffer Control */
#define SCR_CCR		0x98		/* w Clock Control	*/
#define SCR_PLL2CR	0x9a		/* w PLL2 Control	*/
#define SCR_PLL1CR	0x9c		/* l PLL1 Control	*/
#define SCR_DIARCR	0xa0		/* b Device Internal Active Register Control */
#define SCR_DBOCR	0xa1		/* b Device Buffer Off Control */
#define SCR_FER		0xe0		/* b Function Enable	*/
#define SCR_MCR		0xe4		/* w Mode Control	*/
#define SCR_CONFIG	0xfc		/* b Configuration Control */
#define SCR_DEBUG	0xff		/* b Debug		*/

struct tc6393xb_s {
    target_phys_addr_t target_base;
    struct {
        uint8_t ISR;
        uint8_t IMR;
        uint8_t IRR;
        uint16_t GPER;
        uint8_t GPI_SR[3];
        uint8_t GPI_IMR[3];
        uint8_t GPI_EDER[3];
        uint8_t GPI_LIR[3];
        uint8_t GP_IARCR[3];
        uint8_t GP_IARLCR[3];
        uint8_t GPI_BCR[3];
        uint16_t GPA_IARCR;
        uint16_t GPA_IARLCR;
        uint16_t CCR;
        uint16_t PLL2CR;
        uint32_t PLL1CR;
        uint8_t DIARCR;
        uint8_t DBOCR;
        uint8_t FER;
        uint16_t MCR;
        uint8_t CONFIG;
        uint8_t DEBUG;
    } scr;
    uint32_t gpio_dir;
    uint32_t gpio_level;
    uint32_t prev_level;
    qemu_irq handler[TC6393XB_GPIOS];
    qemu_irq *gpio_in;
};

qemu_irq *tc6393xb_gpio_in_get(struct tc6393xb_s *s)
{
    return s->gpio_in;
}

static void tc6393xb_gpio_set(void *opaque, int line, int level)
{
//    struct tc6393xb_s *s = opaque;

    if (line > TC6393XB_GPIOS) {
        printf("%s: No GPIO pin %i\n", __FUNCTION__, line);
        return;
    }

    // FIXME: how does the chip reflect the GPIO input level change?
}

void tc6393xb_gpio_out_set(struct tc6393xb_s *s, int line,
                    qemu_irq handler)
{
    if (line >= TC6393XB_GPIOS) {
        fprintf(stderr, "TC6393xb: no GPIO pin %d\n", line);
        return;
    }

    s->handler[line] = handler;
}

static void tc6393xb_gpio_handler_update(struct tc6393xb_s *s)
{
    uint32_t level, diff;
    int bit;

    level = s->gpio_level & s->gpio_dir;

    for (diff = s->prev_level ^ level; diff; diff ^= 1 << bit) {
        bit = ffs(diff) - 1;
        qemu_set_irq(s->handler[bit], (level >> bit) & 1);
    }

    s->prev_level = level;
}

#define SCR_REG_B(N)                            \
    case SCR_ ##N: return s->scr.N
#define SCR_REG_W(N)                            \
    case SCR_ ##N: return s->scr.N;             \
    case SCR_ ##N + 1: return s->scr.N >> 8;
#define SCR_REG_L(N)                            \
    case SCR_ ##N: return s->scr.N;             \
    case SCR_ ##N + 1: return s->scr.N >> 8;    \
    case SCR_ ##N + 2: return s->scr.N >> 16;   \
    case SCR_ ##N + 3: return s->scr.N >> 24;
#define SCR_REG_A(N)                            \
    case SCR_ ##N(0): return s->scr.N[0];       \
    case SCR_ ##N(1): return s->scr.N[1];       \
    case SCR_ ##N(2): return s->scr.N[2]

static uint32_t tc6393xb_readb(void *opaque, target_phys_addr_t addr)
{
    struct tc6393xb_s *s = opaque;
    addr -= s->target_base;
    switch (addr) {
        case SCR_REVID:
            return 3;
        case SCR_REVID+1:
            return 0;
        SCR_REG_B(ISR);
        SCR_REG_B(IMR);
        SCR_REG_B(IRR);
        SCR_REG_W(GPER);
        SCR_REG_A(GPI_SR);
        SCR_REG_A(GPI_IMR);
        SCR_REG_A(GPI_EDER);
        SCR_REG_A(GPI_LIR);
        case SCR_GPO_DSR(0):
        case SCR_GPO_DSR(1):
        case SCR_GPO_DSR(2):
            return (s->gpio_level >> ((addr - SCR_GPO_DSR(0)) * 8)) & 0xff;
        case SCR_GPO_DOECR(0):
        case SCR_GPO_DOECR(1):
        case SCR_GPO_DOECR(2):
            return (s->gpio_dir >> ((addr - SCR_GPO_DOECR(0)) * 8)) & 0xff;
        SCR_REG_A(GP_IARCR);
        SCR_REG_A(GP_IARLCR);
        SCR_REG_A(GPI_BCR);
        SCR_REG_W(GPA_IARCR);
        SCR_REG_W(GPA_IARLCR);
        SCR_REG_W(CCR);
        SCR_REG_W(PLL2CR);
        SCR_REG_L(PLL1CR);
        SCR_REG_B(DIARCR);
        SCR_REG_B(DBOCR);
        SCR_REG_B(FER);
        SCR_REG_W(MCR);
        SCR_REG_B(CONFIG);
        SCR_REG_B(DEBUG);
    }
    fprintf(stderr, "tc6393xb: unhandled read at %08x\n", (uint32_t) addr);
    return 0;
}
#undef SCR_REG_B
#undef SCR_REG_W
#undef SCR_REG_L
#undef SCR_REG_A

#define SCR_REG_B(N)                                \
    case SCR_ ##N: s->scr.N = value; break;
#define SCR_REG_W(N)                                \
    case SCR_ ##N: s->scr.N = (s->scr.N & ~0xff) | (value & 0xff); break; \
    case SCR_ ##N + 1: s->scr.N = (s->scr.N & 0xff) | (value << 8); break
#define SCR_REG_L(N)                                \
    case SCR_ ##N: s->scr.N = (s->scr.N & ~0xff) | (value & 0xff); break;   \
    case SCR_ ##N + 1: s->scr.N = (s->scr.N & ~(0xff << 8)) | (value & (0xff << 8)); break;     \
    case SCR_ ##N + 2: s->scr.N = (s->scr.N & ~(0xff << 16)) | (value & (0xff << 16)); break;   \
    case SCR_ ##N + 3: s->scr.N = (s->scr.N & ~(0xff << 24)) | (value & (0xff << 24)); break;
#define SCR_REG_A(N)                                \
    case SCR_ ##N(0): s->scr.N[0] = value; break;   \
    case SCR_ ##N(1): s->scr.N[1] = value; break;   \
    case SCR_ ##N(2): s->scr.N[2] = value; break

static void tc6393xb_writeb(void *opaque, target_phys_addr_t addr, uint32_t value)
{
    struct tc6393xb_s *s = opaque;
    addr -= s->target_base;
    switch (addr) {
        SCR_REG_B(ISR);
        SCR_REG_B(IMR);
        SCR_REG_B(IRR);
        SCR_REG_W(GPER);
        SCR_REG_A(GPI_SR);
        SCR_REG_A(GPI_IMR);
        SCR_REG_A(GPI_EDER);
        SCR_REG_A(GPI_LIR);
        case SCR_GPO_DSR(0):
        case SCR_GPO_DSR(1):
        case SCR_GPO_DSR(2):
            s->gpio_level = (s->gpio_level & ~(0xff << ((addr - SCR_GPO_DSR(0))*8))) | ((value & 0xff) << ((addr - SCR_GPO_DSR(0))*8));
            tc6393xb_gpio_handler_update(s);
            break;
        case SCR_GPO_DOECR(0):
        case SCR_GPO_DOECR(1):
        case SCR_GPO_DOECR(2):
            s->gpio_dir = (s->gpio_dir & ~(0xff << ((addr - SCR_GPO_DOECR(0))*8))) | ((value & 0xff) << ((addr - SCR_GPO_DOECR(0))*8));
            tc6393xb_gpio_handler_update(s);
            break;
        SCR_REG_A(GP_IARCR);
        SCR_REG_A(GP_IARLCR);
        SCR_REG_A(GPI_BCR);
        SCR_REG_W(GPA_IARCR);
        SCR_REG_W(GPA_IARLCR);
        SCR_REG_W(CCR);
        SCR_REG_W(PLL2CR);
        SCR_REG_L(PLL1CR);
        SCR_REG_B(DIARCR);
        SCR_REG_B(DBOCR);
        SCR_REG_B(FER);
        SCR_REG_W(MCR);
        SCR_REG_B(CONFIG);
        SCR_REG_B(DEBUG);
        default:
            fprintf(stderr, "tc6393xb: unhandled write at %08x: %02x\n",
					(uint32_t) addr, value & 0xff);
            break;
    }
}
#undef SCR_REG_B
#undef SCR_REG_W
#undef SCR_REG_L
#undef SCR_REG_A

static uint32_t tc6393xb_readw(void *opaque, target_phys_addr_t addr)
{
    return (tc6393xb_readb(opaque, addr) & 0xff) |
        (tc6393xb_readb(opaque, addr + 1) << 8);
}

static uint32_t tc6393xb_readl(void *opaque, target_phys_addr_t addr)
{
    return (tc6393xb_readb(opaque, addr) & 0xff) |
        ((tc6393xb_readb(opaque, addr + 1) & 0xff) << 8) |
        ((tc6393xb_readb(opaque, addr + 2) & 0xff) << 16) |
        ((tc6393xb_readb(opaque, addr + 3) & 0xff) << 24);
}

static void tc6393xb_writew(void *opaque, target_phys_addr_t addr, uint32_t value)
{
    tc6393xb_writeb(opaque, addr, value);
    tc6393xb_writeb(opaque, addr + 1, value >> 8);
}

static void tc6393xb_writel(void *opaque, target_phys_addr_t addr, uint32_t value)
{
    tc6393xb_writeb(opaque, addr, value);
    tc6393xb_writeb(opaque, addr + 1, value >> 8);
    tc6393xb_writeb(opaque, addr + 2, value >> 16);
    tc6393xb_writeb(opaque, addr + 3, value >> 24);
}

struct tc6393xb_s *tc6393xb_init(uint32_t base, qemu_irq irq)
{
    int iomemtype;
    struct tc6393xb_s *s;
    CPUReadMemoryFunc *tc6393xb_readfn[] = {
        tc6393xb_readb,
        tc6393xb_readw,
        tc6393xb_readl,
    };
    CPUWriteMemoryFunc *tc6393xb_writefn[] = {
        tc6393xb_writeb,
        tc6393xb_writew,
        tc6393xb_writel,
    };

    s = (struct tc6393xb_s *) qemu_mallocz(sizeof(struct tc6393xb_s));
    s->target_base = base;
    s->gpio_in = qemu_allocate_irqs(tc6393xb_gpio_set, s, TC6393XB_GPIOS);

    iomemtype = cpu_register_io_memory(0, tc6393xb_readfn,
                    tc6393xb_writefn, s);
    cpu_register_physical_memory(s->target_base, 0x200000, iomemtype);

    return s;
}
