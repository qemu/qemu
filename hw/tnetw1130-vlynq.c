/*
 * QEMU emulation for Texas Instruments TNETW1130 (ACX111) wireless.
 *
 * Copyright (C) 2007-2010 Stefan Weil
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Texas Instruments does not provide any datasheets.
 *
 * TODO:
 * - Add save, load support.
 * - Much more emulation is needed.
 */

#include <assert.h>             /* assert */
#include "hw.h"
#include "net.h"
#include "pci.h"
#include "tnetw1130.h"
#include "vlynq.h"

/*****************************************************************************
 *
 * Common declarations.
 *
 ****************************************************************************/

#define BIT(n) (1 << (n))
#define BITS(n, m) (((0xffffffffU << (31 - n)) >> (31 - n + m)) << m)

#define KiB 1024

/*****************************************************************************
 *
 * Declarations for emulation options and debugging.
 *
 ****************************************************************************/

/* Debug TNETW1130 card. */
#define DEBUG_TNETW1130

#if defined(DEBUG_TNETW1130)
# define logout(fmt, ...) fprintf(stderr, "ACX111\t%-24s" fmt, __func__, ##__VA_ARGS__)
#else
# define logout(fmt, ...) ((void)0)
#endif

#define missing(text)       assert(!"feature is missing in this emulation: " text)
#define MISSING() logout("%s:%u missing, %s!!!\n", __FILE__, __LINE__, backtrace())
#define UNEXPECTED() logout("%s:%u unexpected, %s!!!\n", __FILE__, __LINE__, backtrace())
#define backtrace() ""

/* Enable or disable logging categories. */
#define LOG_PHY         1
#define LOG_RX          1       /* receive messages */
#define LOG_TX          1       /* transmit messages */

#if defined(DEBUG_TNETW1130)
# define TRACE(condition, command) ((condition) ? (command) : (void)0)
#else
# define TRACE(condition, command) ((void)0)
#endif

#define TNETW1130_FW_SIZE        (128 * KiB)

typedef struct {
    VLYNQDevice dev;
    tnetw1130_t tnetw1130;
} vlynq_tnetw1130_t;

/*****************************************************************************
 *
 * Helper functions.
 *
 ****************************************************************************/

#if defined(DEBUG_TNETW1130)
static uint32_t traceflags = 1;

#define TNETW   traceflags

#define SET_TRACEFLAG(name) \
    do { \
        char *substring = strstr(envvalue, #name); \
        if (substring) { \
            name = ((substring > envvalue && substring[-1] == '-') ? 0 : 1); \
        } \
        TRACE(name, logout("Logging enabled for " #name "\n")); \
    } while(0)

static void set_traceflags(const char *envname)
{
    const char *envvalue = getenv(envname);
    if (envvalue != 0) {
        unsigned long ul = strtoul(envvalue, 0, 0);
        if ((ul == 0) && strstr(envvalue, "ALL")) ul = 0xffffffff;
        traceflags = ul;
        SET_TRACEFLAG(TNETW);
    }
}
#endif /* DEBUG_TNETW1130 */

static void reg_write16(uint8_t * reg, uint32_t addr, uint16_t value)
{
    assert(!(addr & 1));
    *(uint16_t *) (&reg[addr]) = cpu_to_le16(value);
}

static void tnetw1130_mem_map(VLYNQDevice *vlynq_dev, int region_num,
                              pcibus_t addr, pcibus_t size, int type)
{
    vlynq_tnetw1130_t *d = (vlynq_tnetw1130_t *)vlynq_dev;
    tnetw1130_t *s = &d->tnetw1130;

    TRACE(TNETW, logout("region %d, addr 0x%08" FMT_PCIBUS
                        ", size 0x%08" FMT_PCIBUS "\n",
                        region_num, addr, size));
    assert((unsigned)region_num < TNETW1130_REGIONS);
    s->region[region_num] = addr;

    cpu_register_physical_memory(addr, size, s->io_memory[region_num]);
}

static int vlynq_tnetw1130_init(VLYNQDevice* vlynq_dev)
{
    vlynq_tnetw1130_t *d = DO_UPCAST(vlynq_tnetw1130_t, dev, vlynq_dev);
    //~ uint8_t *pci_conf = d->dev.config;
    tnetw1130_t *s = &d->tnetw1130;
#if defined(DEBUG_TNETW1130)
    set_traceflags("DEBUG_AR7");
#endif
    TRACE(TNETW, logout("\n"));
    /* TI TNETW1130 */
    //~ tnetw1130_pci_config(pci_conf);

    /* Handler for memory-mapped I/O */
    s->io_memory[0] =
        cpu_register_io_memory(tnetw1130_region0_read, tnetw1130_region0_write, d);
    s->io_memory[1] =
        cpu_register_io_memory(tnetw1130_region1_read, tnetw1130_region1_write, d);

    TRACE(TNETW, logout("io_memory = 0x%08x, 0x%08x\n", s->io_memory[0], s->io_memory[1]));

    //~ pci_register_bar(&d->dev, 0, TNETW1130_MEM0_SIZE,
                     //~ PCI_BASE_ADDRESS_SPACE_MEMORY, tnetw1130_mem_map);
    //~ pci_register_bar(&d->dev, 1, TNETW1130_MEM1_SIZE,
                     //~ PCI_BASE_ADDRESS_SPACE_MEMORY, tnetw1130_mem_map);

    //~ memcpy(s->mem1 + 0x0001f000, pci_conf, 64);

    /* eCPU is halted. */
    reg_write16(s->mem0, TNETW1130_ECPU_CTRL, 1);

    //~ tnetw1130_mem_map(&d->dev, 0, 0x04000000, 0x22000, 0);  /* 0xf0000000 */
    //~ tnetw1130_mem_map(&d->dev, 1, 0x04022000, 0x40000, 0);  /* 0xc0000000 */
    //~ tnetw1130_mem_map(&d->dev, 1, 0x04000000, 0x40000, 0);
    //~ tnetw1130_mem_map(&d->dev, 0, 0x04040000, 0x22000, 0);
    tnetw1130_mem_map(&d->dev, 0, 0x04000000, TNETW1130_MEM0_SIZE, 0);
    tnetw1130_mem_map(&d->dev, 1, 0x04022000, TNETW1130_MEM1_SIZE, 0);
    return 0;
}

static int vlynq_tnetw1130_uninit(VLYNQDevice *vlynq_dev)
{
    vlynq_tnetw1130_t *d = DO_UPCAST(vlynq_tnetw1130_t, dev, vlynq_dev);
    tnetw1130_t *s = &d->tnetw1130;

    cpu_unregister_io_memory(s->io_memory[0]);
    cpu_unregister_io_memory(s->io_memory[1]);
    //~ vmstate_unregister(s->vmstate, s);
    qemu_del_vlan_client(&s->nic->nc);
    return 0;
}

static VLYNQDeviceInfo vlynq_tnetw1130_info = {
    .qdev.name = "tnetw1130-vlynq",
    .qdev.desc = "Texas Instruments TNETW1130 (VLYNQ)",
    .qdev.size = sizeof(vlynq_tnetw1130_t),
    .qdev.props = (Property[]) {
        DEFINE_NIC_PROPERTIES(vlynq_tnetw1130_t, tnetw1130.conf),
        DEFINE_PROP_END_OF_LIST(),
    },
    .init      = vlynq_tnetw1130_init,
    .exit      = vlynq_tnetw1130_uninit,
};

static void tnetw1130_register_device(void)
{
    vlynq_qdev_register(&vlynq_tnetw1130_info);
}

device_init(tnetw1130_register_device)

/* eof */
