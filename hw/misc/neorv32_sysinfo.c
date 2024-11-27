#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "system/address-spaces.h"
#include "neorv32_sysinfo.h" /* QEMU related */
#include "neorv32_sysinfo_rtl.h" /* RTL related */


/* Register addresses (offsets) */
enum {
	REG_SYSINFO_CLK    = 0x00,
	REG_SYSINFO_MISC   = 0x04,
	REG_SYSINFO_SOC    = 0x08,
	REG_SYSINFO_CACHE  = 0x0C,
};


typedef struct Neorv32SysInfoState {
    MemoryRegion mmio;
    uint32_t clk_hz;   /* rw */
    uint32_t misc;     /* ro */
    uint32_t soc;      /* ro */
    uint32_t cache;    /* ro */
} Neorv32SysInfoState;


/* Safe integer log2: assumes power-of-two sizes; returns 0 if size is 0 */
static unsigned int neorv32_log2u(uint32_t x)
{
    if (x == 0U) {
        return 0U;
    }
    unsigned int r = 0U;
    while ((x >>= 1U) != 0U) {
        r++;
    }
    return r;
}

/* Compose MISC register per the firmware header */
static uint32_t neorv32_sysinfo_build_misc(void)
{
    const uint32_t imem_log2  = neorv32_log2u(SYSINFO_IMEM_SIZE) & 0xFFU;  /* [7:0]  */
    const uint32_t dmem_log2  = neorv32_log2u(SYSINFO_DMEM_SIZE) & 0xFFU;  /* [15:8] */
    const uint32_t harts      = (SYSINFO_NUM_HARTS & 0x0FU);               /* [19:16] */
    const uint32_t bootmode   = (SYSINFO_BOOTMODE_ID & 0x03U);             /* [21:20] */
    const uint32_t intbus_to  = (SYSINFO_INTBUS_TO_LOG2 & 0x1FU);          /* [26:22] */
    const uint32_t extbus_to  = (SYSINFO_EXTBUS_TO_LOG2 & 0x1FU);          /* [31:27] */

    uint32_t v = 0U;
    v |= (imem_log2 << 0);
    v |= (dmem_log2 << 8);
    v |= (harts     << 16);
    v |= (bootmode  << 20);
    v |= (intbus_to << 22);
    v |= (extbus_to << 27);
    return v;
}

/* Compose CACHE register per the firmware header */
static uint32_t neorv32_sysinfo_build_cache(void)
{
    uint32_t v = 0U;
    v |= ((ICACHE_BLOCK_SIZE_LOG2 & 0x0FU) << 0);
    v |= ((ICACHE_NUM_BLOCKS_LOG2 & 0x0FU) << 4);
    v |= ((DCACHE_BLOCK_SIZE_LOG2 & 0x0FU) << 8);
    v |= ((DCACHE_NUM_BLOCKS_LOG2 & 0x0FU) << 12);
    v |= ((ICACHE_BURSTS_EN ? 1U : 0U) << 16);
    v |= ((DCACHE_BURSTS_EN ? 1U : 0U) << 24);
    return v;
}

static uint64_t neorv32_sysinfo_read(void *opaque, hwaddr addr, unsigned size)
{
    Neorv32SysInfoState *s = opaque;
    uint32_t val = 0U;

    switch (addr) {
    case REG_SYSINFO_CLK:
        val = s->clk_hz;
        break;
    case REG_SYSINFO_MISC:
        val = s->misc;
        break;
    case REG_SYSINFO_SOC:
        val = s->soc;
        break;
    case REG_SYSINFO_CACHE:
        val = s->cache;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid read addr=0x%" HWADDR_PRIx " size=%u\n",
                      __func__, addr, size);
        return 0;
    }

    /* Enforce access size semantics (1/2/4 ok); we just return the low bytes */
    switch (size) {
    case 4: return val;
    case 2: return (uint16_t)val;
    case 1: return (uint8_t)val;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid read size=%u at addr=0x%" HWADDR_PRIx "\n",
                      __func__, size, addr);
        return 0;
    }
}

static void neorv32_sysinfo_write(void *opaque, hwaddr addr, uint64_t data, unsigned size)
{
    Neorv32SysInfoState *s = opaque;

    /* Only CLK is writable; others are read-only */
    if (addr == REG_SYSINFO_CLK) {
        /* Accept 1/2/4 byte writes; update the corresponding bytes of clk_hz */
        uint32_t old = s->clk_hz;
        uint32_t val = old;

        switch (size) {
        case 4:
            val = (uint32_t)data;
            break;
        case 2: {
            uint16_t part = (uint16_t)data;
            /* Little-endian halfword at offset (0 or 2) */
            if ((addr & 0x3) == 0x0) {
                val = (old & 0xFFFF0000U) | part;
            } else if ((addr & 0x3) == 0x2) {
                val = (old & 0x0000FFFFU) | ((uint32_t)part << 16);
            } else {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "%s: misaligned 16-bit write at 0x%" HWADDR_PRIx "\n",
                              __func__, addr);
                return;
            }
            break;
        }
        case 1: {
            uint8_t part = (uint8_t)data;
            uint32_t shift = (addr & 0x3) * 8U;
            val = (old & ~(0xFFU << shift)) | ((uint32_t)part << shift);
            break;
        }
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: invalid write size=%u at addr=0x%" HWADDR_PRIx "\n",
                          __func__, size, addr);
            return;
        }

        s->clk_hz = val;
        return;
    }

    qemu_log_mask(LOG_GUEST_ERROR,
                  "%s: write to read-only addr=0x%" HWADDR_PRIx " val=0x%" PRIx64 " size=%u\n",
                  __func__, addr, data, size);
}

static const MemoryRegionOps neorv32_sysinfo_ops = {
    .read = neorv32_sysinfo_read,
    .write = neorv32_sysinfo_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
};

void neorv32_sysinfo_create(MemoryRegion *address_space, hwaddr base)
{
    Neorv32SysInfoState *s = g_new0(Neorv32SysInfoState, 1);

    s->clk_hz = SYSINFO_CLK_HZ_DEFAULT;
    s->misc   = neorv32_sysinfo_build_misc();
    s->soc    = SYSINFO_SOC_VAL;
    s->cache  = neorv32_sysinfo_build_cache();

    memory_region_init_io(&s->mmio, NULL, &neorv32_sysinfo_ops,
                          s, "neorv32.sysinfo", 16 /* 4 regs x 4 bytes */);

    memory_region_add_subregion(address_space, base, &s->mmio);
}
