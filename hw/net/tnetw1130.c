/*
 * QEMU emulation for Texas Instruments TNETW1130 (ACX111) wireless.
 *
 * Copyright (C) 2007-2012 Stefan Weil
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
#include "net/net.h"
#include "pci/pci.h"

#include "tnetw1130.h"

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

//~ static int tnetw1130_instance = 0;

typedef struct {
    PCIDevice dev;
    MemoryRegion mmio_bar0;
    MemoryRegion mmio_bar1;
    tnetw1130_t tnetw1130;
} TNETW1130State;

typedef enum {
    CMD_MAILBOX = 0x0001e108,   /* ECPU_CTRL? */
    INFO_MAILBOX = 0x0001e0f0,  /* HINT_STS_ND? */
} tnetw1130_memory_offset_t;

typedef enum {
    ACX1xx_CMD_RESET = 0x00,
    ACX1xx_CMD_INTERROGATE = 0x01,
    ACX1xx_CMD_CONFIGURE = 0x02,
    ACX1xx_CMD_ENABLE_RX = 0x03,
    ACX1xx_CMD_ENABLE_TX = 0x04,
    ACX1xx_CMD_DISABLE_RX = 0x05,
    ACX1xx_CMD_DISABLE_TX = 0x06,
    ACX1xx_CMD_FLUSH_QUEUE  = 0x07,
    ACX1xx_CMD_SCAN = 0x08,
    ACX1xx_CMD_STOP_SCAN = 0x09,
    ACX1xx_CMD_CONFIG_TIM = 0x0a,
    ACX1xx_CMD_JOIN = 0x0b,
    ACX1xx_CMD_WEP_MGMT = 0x0c,
#ifdef OLD_FIRMWARE_VERSIONS
    ACX100_CMD_HALT = 0x0e,             /* mapped to unknownCMD in FW150 */
#else
    ACX1xx_CMD_MEM_READ = 0x0d,
    ACX1xx_CMD_MEM_WRITE = 0x0e,
#endif
    ACX1xx_CMD_SLEEP = 0x0f,
    ACX1xx_CMD_WAKE = 0x10,
    ACX1xx_CMD_UNKNOWN_11 = 0x11,       /* mapped to unknownCMD in FW150 */
    ACX100_CMD_INIT_MEMORY = 0x12,
    ACX1xx_CMD_DISABLE_RADIO = 0x12,    /* new firmware? TNETW1450? */
    ACX1xx_CMD_CONFIG_BEACON = 0x13,
    ACX1xx_CMD_CONFIG_PROBE_RESPONSE = 0x14,
    ACX1xx_CMD_CONFIG_NULL_DATA = 0x15,
    ACX1xx_CMD_CONFIG_PROBE_REQUEST = 0x16,
    ACX1xx_CMD_FCC_TEST = 0x17,
    ACX1xx_CMD_RADIOINIT = 0x18,
    ACX111_CMD_RADIOCALIB = 0x19,
    ACX1FF_CMD_NOISE_HISTOGRAM = 0x1c,  /* new firmware? TNETW1450? */
    ACX1FF_CMD_RX_RESET = 0x1d,         /* new firmware? TNETW1450? */
    ACX1FF_CMD_LNA_CONTROL = 0x20,      /* new firmware? TNETW1450? */
    ACX1FF_CMD_CONTROL_DBG_TRACE = 0x21, /* new firmware? TNETW1450? */
} tnetw1130_command_t;

/* IRQ Constants */
typedef enum {
    HOST_INT_RX_DATA = BIT(0),
#define HOST_INT_TX_COMPLETE	0x0002
#define HOST_INT_TX_XFER	0x0004
#define HOST_INT_RX_COMPLETE	0x0008
#define HOST_INT_DTIM		0x0010
#define HOST_INT_BEACON		0x0020
#define HOST_INT_TIMER		0x0040
#define HOST_INT_KEY_NOT_FOUND	0x0080
#define HOST_INT_IV_ICV_FAILURE	0x0100
#define HOST_INT_CMD_COMPLETE	0x0200
#define HOST_INT_INFO		0x0400
#define HOST_INT_OVERFLOW	0x0800
#define HOST_INT_PROCESS_ERROR	0x1000
#define HOST_INT_SCAN_COMPLETE	0x2000
#define HOST_INT_FCS_THRESHOLD	0x4000
#define HOST_INT_UNKNOWN	0x8000
} tnetw10030_irq_bit_t;

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

static uint16_t reg_read16(const uint8_t * reg, uint32_t addr)
{
    assert(!(addr & 1));
    return le16_to_cpu(*(uint16_t *) (&reg[addr]));
}

static void reg_write16(uint8_t * reg, uint32_t addr, uint16_t value)
{
    assert(!(addr & 1));
    *(uint16_t *) (&reg[addr]) = cpu_to_le16(value);
}

static uint32_t reg_read32(const uint8_t * reg, uint32_t addr)
{
    assert(!(addr & 3));
    return le32_to_cpu(*(uint32_t *) (&reg[addr]));
}

static void reg_write32(uint8_t * reg, uint32_t addr, uint32_t value)
{
    assert(!(addr & 3));
    *(uint32_t *) (&reg[addr]) = cpu_to_le32(value);
}

#if defined(DEBUG_TNETW1130)

typedef struct {
    unsigned offset;
    const char *name;
} offset_name_t;

static const char *offset2name(const offset_name_t *o2n, unsigned offset)
{
    static char buffer[32];
    const char *name = buffer;
    sprintf(buffer, "0x%08x", offset);
    for (; o2n->name != 0; o2n++) {
        if (offset == o2n->offset) {
            name = o2n->name;
            break;
        }
    }
    return name;
}

#define ENTRY(entry) { TNETW1130_##entry, #entry }
static const offset_name_t addr2reg[] = {
    ENTRY(SOFT_RESET),
    ENTRY(SLV_MEM_ADDR),
    ENTRY(SLV_MEM_DATA),
    ENTRY(SLV_MEM_CTL),
    ENTRY(IRQ_MASK),
    ENTRY(IRQ_STATUS_CLEAR),
    ENTRY(IRQ_ACK),
    ENTRY(HINT_TRIG),
    ENTRY(IRQ_STATUS_NON_DES),
    ENTRY(EE_START),
    ENTRY(ECPU_CTRL),
    ENTRY(ENABLE),
    ENTRY(EEPROM_CTL),
    ENTRY(EEPROM_ADDR),
    ENTRY(EEPROM_DATA),
    ENTRY(EEPROM_CFG),
    ENTRY(PHY_ADDR),
    ENTRY(PHY_DATA),
    ENTRY(PHY_CTL),
    ENTRY(GPIO_OE),
    ENTRY(GPIO_OUT),
    ENTRY(CMD_MAILBOX_OFFS),
    ENTRY(INFO_MAILBOX_OFFS),
    ENTRY(EEPROM_INFORMATION),
    { 0 }
};

static const char *tnetw1130_regname(unsigned addr)
{
    return offset2name(addr2reg, addr);
}

static const char *tnetw1130_regname1(unsigned addr)
{
    static char buffer[32];
    const char *name = buffer;
    sprintf(buffer, "0x%08x", addr);
    switch (addr) {
        case CMD_MAILBOX:
            name = "CMD_MAILBOX";
            break;
        case INFO_MAILBOX:
            name = "INFO_MAILBOX";
            break;
    }
    return name;
}

#undef ENTRY
#define ENTRY(entry) { ACX1xx_CMD_##entry, #entry }
static const offset_name_t cmd2name[] = {
    ENTRY(RESET),
    ENTRY(INTERROGATE),
    ENTRY(CONFIGURE),
    ENTRY(ENABLE_RX),
    ENTRY(ENABLE_TX),
    ENTRY(DISABLE_RX),
    ENTRY(DISABLE_TX),
    ENTRY(FLUSH_QUEUE),
    ENTRY(SCAN),
    ENTRY(STOP_SCAN),
    ENTRY(CONFIG_TIM),
    ENTRY(JOIN),
    ENTRY(WEP_MGMT),
    ENTRY(MEM_READ),
    ENTRY(MEM_WRITE),
    ENTRY(SLEEP),
    ENTRY(WAKE),
    ENTRY(UNKNOWN_11),
    //~ ENTRY(INIT_MEMORY),
    ENTRY(DISABLE_RADIO),
    ENTRY(CONFIG_BEACON),
    ENTRY(CONFIG_PROBE_RESPONSE),
    ENTRY(CONFIG_NULL_DATA),
    ENTRY(CONFIG_PROBE_REQUEST),
    { 0 }
};

static const char *tnetw1130_cmdname(uint16_t cmd)
{
    return offset2name(cmd2name, cmd);
}

#endif /* DEBUG_TNETW1130 */

static void tnetw1130_cmd_reset(tnetw1130_t *s)
{
}

static void tnetw1130_cmd_interrogate(tnetw1130_t *s)
{
}

static void tnetw1130_cmd(tnetw1130_t *s)
{
    uint16_t cmd = reg_read16(s->mem1, CMD_MAILBOX);
    s->irq_status |= HOST_INT_CMD_COMPLETE;
    reg_write16(s->mem1, CMD_MAILBOX + 2, 0x0001);
    switch (cmd) {
        case ACX1xx_CMD_RESET:                  /* 0x00 */
            tnetw1130_cmd_reset(s);
            break;
        case ACX1xx_CMD_INTERROGATE:            /* 0x01 */
            tnetw1130_cmd_interrogate(s);
            break;
        case ACX1xx_CMD_CONFIGURE:              /* 0x02 */
        case ACX1xx_CMD_CONFIG_TIM:             /* 0x0a */
        case ACX1xx_CMD_CONFIG_BEACON:          /* 0x13 */
        case ACX1xx_CMD_CONFIG_PROBE_RESPONSE:  /* 0x14 */
        case ACX1xx_CMD_CONFIG_NULL_DATA:       /* 0x15 */
        case ACX1xx_CMD_CONFIG_PROBE_REQUEST:   /* 0x16 */
            break;
    }
}

static void tnetw1130_reset(tnetw1130_t *s)
{
    MISSING();
}

static uint8_t tnetw1130_read0b(tnetw1130_t *s, hwaddr addr)
{
    uint8_t value = 0;
    if (addr < TNETW1130_MEM0_SIZE) {
        value = s->mem0[addr];
    } else {
        UNEXPECTED();
    }
    //~ } else if (addr -= 0x20000, addr == TNETW1130_SOFT_RESET) {
    TRACE(TNETW, logout("addr %s = 0x%02x\n", tnetw1130_regname(addr), value));
    return value;
}

/* Radio type names. */
typedef enum {
    RADIO_MAXIM_0D = 0x0d,
    RADIO_RFMD_11 = 0x11,
    RADIO_RALINK_15 = 0x15,
    /* used in ACX111 cards (WG311v2, WL-121, ...): */
    RADIO_RADIA_16 = 0x16,
} radio_t;

static uint16_t tnetw1130_read0w(tnetw1130_t *s, hwaddr addr)
{
    uint16_t value = 0;
    if (addr < TNETW1130_MEM0_SIZE) {
        value = reg_read16(s->mem0, addr);
    }
    if (0) {
    } else if (addr == TNETW1130_SOFT_RESET) {
    } else if (addr == TNETW1130_IRQ_STATUS_NON_DES) {
        /* !!! set after eCPU start */
        value = s->irq_status;
    } else if (addr == TNETW1130_EE_START) {
    } else if (addr == TNETW1130_ECPU_CTRL) {
    } else if (addr == TNETW1130_EEPROM_CTL) {
        value = 0;
    } else if (addr == TNETW1130_EEPROM_INFORMATION) {
        value = (RADIO_RADIA_16 << 8) + 0x01;
    }
    TRACE(TNETW, logout("addr %s = 0x%04x\n", tnetw1130_regname(addr), value));
    return value;
}

static uint32_t tnetw1130_read0l(tnetw1130_t *s, hwaddr addr)
{
    uint32_t value = 0;
    assert(addr < TNETW1130_MEM0_SIZE);
    value = reg_read32(s->mem0, addr);
    if (0) {
    } else if (addr == TNETW1130_SLV_MEM_DATA) {
        value = reg_read32(s->fw, s->fw_addr);
    } else if (addr == TNETW1130_CMD_MAILBOX_OFFS) {
        value = CMD_MAILBOX;
    } else if (addr == TNETW1130_INFO_MAILBOX_OFFS) {
        value = INFO_MAILBOX;
    }
    TRACE(TNETW, logout("addr %s = 0x%08x\n", tnetw1130_regname(addr), value));
    return value;
}

static void tnetw1130_write0b(tnetw1130_t *s, hwaddr addr,
                              uint8_t value)
{
    if (addr < TNETW1130_MEM0_SIZE) {
        s->mem0[addr] = value;
    } else {
        UNEXPECTED();
    }
    TRACE(TNETW, logout("addr %s = 0x%02x\n", tnetw1130_regname(addr), value));
}

static void tnetw1130_write0w(tnetw1130_t *s, hwaddr addr,
                              uint16_t value)
{
    if (addr < TNETW1130_MEM0_SIZE) {
        reg_write16(s->mem0, addr, value);
    } else {
        UNEXPECTED();
    }
    if (addr == TNETW1130_SOFT_RESET) {
        if (value & 1) {
            TRACE(TNETW, logout("soft reset\n"));
        }
    } else if (addr == TNETW1130_INT_TRIG) {
        if (value == 1) {
            TRACE(TNETW, logout("trigger interrupt, status, cmd = %s\n",
                   tnetw1130_cmdname(reg_read16(s->mem1, CMD_MAILBOX))));
            tnetw1130_cmd(s);
        } else {
            UNEXPECTED();
        }
    } else if (addr == TNETW1130_IRQ_ACK) {
        /* !!! must reset irq */
        s->irq_status &= ~value;
    } else if (addr == TNETW1130_EE_START) {
        if (value & 1) {
            TRACE(TNETW, logout("start burst read from EEPROM\n"));
        }
    } else if (addr == TNETW1130_ECPU_CTRL) {
        if (value & 1) {
            TRACE(TNETW, logout("halt eCPU\n"));
            //~ reg_write16(s->mem0, addr, value & ~1);
        } else {
            TRACE(TNETW, logout("start eCPU\n"));
            s->irq_status |= HOST_INT_FCS_THRESHOLD;
        }
    }
    TRACE(TNETW, logout("addr %s = 0x%04x\n", tnetw1130_regname(addr), value));
}

static void tnetw1130_write0l(tnetw1130_t *s, hwaddr addr,
                              uint32_t value)
{
    if (addr < TNETW1130_MEM0_SIZE) {
        reg_write32(s->mem0, addr, value);
    }
    if (addr == TNETW1130_SLV_MEM_ADDR) {
        s->fw_addr = value;
        if (value >= TNETW1130_FW_SIZE) {
            UNEXPECTED();
        }
    } else if (addr == TNETW1130_SLV_MEM_DATA) {
        reg_write32(s->fw, s->fw_addr, value);
    } else if (addr == TNETW1130_SLV_MEM_CTL) {
        if (value == 0) {
            TRACE(TNETW, logout("basic mode\n"));
        } else if (value == 1) {
            TRACE(TNETW, logout("autoincrement mode\n"));
            MISSING();
        } else {
            UNEXPECTED();
        }
    } else if (addr == TNETW1130_SLV_END_CTL) {
    }
    TRACE(TNETW, logout("addr %s = 0x%08x\n", tnetw1130_regname(addr), value));
}

static uint8_t tnetw1130_read1b(tnetw1130_t *s, hwaddr addr)
{
    uint8_t value = 0;
    assert(addr < TNETW1130_MEM1_SIZE);
    value = s->mem1[addr];
    TRACE(TNETW, logout("addr %s = 0x%02x\n", tnetw1130_regname1(addr), value));
    return value;
}

static uint16_t tnetw1130_read1w(tnetw1130_t *s, hwaddr addr)
{
    uint16_t value = 0;
    assert(addr < TNETW1130_MEM1_SIZE);
    value = reg_read16(s->mem1, addr);
    TRACE(TNETW, logout("addr %s = 0x%04x\n", tnetw1130_regname1(addr), value));
    return value;
}

static uint32_t tnetw1130_read1l(tnetw1130_t *s, hwaddr addr)
{
    assert(addr < TNETW1130_MEM1_SIZE);
    uint32_t value = reg_read32(s->mem1, addr);
    TRACE(TNETW, logout("addr %s = 0x%08x\n", tnetw1130_regname1(addr), value));
    return value;
}

static void tnetw1130_write1b(tnetw1130_t *s, hwaddr addr,
                              uint8_t value)
{
    assert(addr < TNETW1130_MEM1_SIZE);
    s->mem1[addr] = value;
    TRACE(TNETW, logout("addr %s = 0x%02x\n", tnetw1130_regname1(addr), value));
}

static void tnetw1130_write1w(tnetw1130_t *s, hwaddr addr,
                              uint16_t value)
{
    assert(addr < TNETW1130_MEM1_SIZE);
    reg_write16(s->mem1, addr, value);
    TRACE(TNETW, logout("addr %s = 0x%04x\n", tnetw1130_regname1(addr), value));
}

static void tnetw1130_write1l(tnetw1130_t *s, hwaddr addr,
                              uint32_t value)
{
    assert(addr < TNETW1130_MEM1_SIZE);
    reg_write32(s->mem1, addr, value);
    TRACE(TNETW, logout("addr %s = 0x%08x\n", tnetw1130_regname1(addr), value));
}

/*****************************************************************************
 *
 * Memory mapped I/O.
 *
 ****************************************************************************/

static uint64_t tnetw1130_read0(void *opaque, hwaddr addr,
                                unsigned size)
{
    TNETW1130State *d = opaque;
    tnetw1130_t *s = &d->tnetw1130;
    uint64_t val = 0;
    switch (size) {
    case 1:
        val = tnetw1130_read0b(s, addr);
        break;
    case 2:
        val = tnetw1130_read0w(s, addr);
        break;
    case 4:
        val = tnetw1130_read0l(s, addr);
        break;
    default:
        assert(!"bad size");
    }
    return val;
}

static void tnetw1130_write0(void *opaque, hwaddr addr,
                             uint64_t val, unsigned size)
{
    TNETW1130State *d = opaque;
    tnetw1130_t *s = &d->tnetw1130;
    switch (size) {
    case 1:
        tnetw1130_write0b(s, addr, val);
        break;
    case 2:
        tnetw1130_write0w(s, addr, val);
        break;
    case 4:
        tnetw1130_write0l(s, addr, val);
        break;
    default:
        assert(!"bad size");
    }
}

static const MemoryRegionOps tnetw1130_ops0 = {
    .read = tnetw1130_read0,
    .write = tnetw1130_write0,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static uint64_t tnetw1130_read1(void *opaque, hwaddr addr,
                                unsigned size)
{
    TNETW1130State *d = opaque;
    tnetw1130_t *s = &d->tnetw1130;
    uint64_t val = 0;
    switch (size) {
    case 1:
        val = tnetw1130_read1b(s, addr);
        break;
    case 2:
        val = tnetw1130_read1w(s, addr);
        break;
    case 4:
        val = tnetw1130_read1l(s, addr);
        break;
    default:
        assert(!"bad size");
    }
    return val;
}

static void tnetw1130_write1(void *opaque, hwaddr addr,
                             uint64_t val, unsigned size)
{
    TNETW1130State *d = opaque;
    tnetw1130_t *s = &d->tnetw1130;
    switch (size) {
    case 1:
        tnetw1130_write1b(s, addr, val);
        break;
    case 2:
        tnetw1130_write1w(s, addr, val);
        break;
    case 4:
        tnetw1130_write1l(s, addr, val);
        break;
    default:
        assert(!"bad size");
    }
}

static const MemoryRegionOps tnetw1130_ops1 = {
    .read = tnetw1130_read1,
    .write = tnetw1130_write1,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

/*****************************************************************************
 *
 * Other functions.
 *
 ****************************************************************************/

static void nic_reset(void *opaque)
{
    //~ TNETW1130State *d = (TNETW1130State *) opaque;
    TRACE(TNETW, logout("%p\n", opaque));
}

static int nic_can_receive(NetClientState *ncs)
{
    //~ tnetw1130_t *s = qemu_get_nic_opaque(ncs);

    TRACE(TNETW, logout("\n"));

    /* TODO: handle queued receive data. */
    return 0;
}

static ssize_t nic_receive(NetClientState *ncs,
                           const uint8_t * buf, size_t size)
{
    TRACE(TNETW, logout("\n"));
    return size;
}

static void nic_cleanup(NetClientState *ncs)
{
#if 0
    tnetw1130_t *s = qemu_get_nic_opaque(ncs);
    timer_del(d->poll_timer);
    timer_free(d->poll_timer);
#endif
}

static void tnetw1130_pci_config(uint8_t *pci_conf)
{
    pci_set_word(pci_conf + PCI_STATUS, 0x0210);
    pci_set_long(pci_conf + PCI_CARDBUS_CIS, 0x00001c02);
    /* Address registers are set by pci_register_bar. */
    /* Capabilities Pointer, CLOFS */
    pci_set_long(pci_conf + PCI_CAPABILITY_LIST, 0x00000040);
    /* 0x38 reserved, returns 0 */
    /* MNGNT = 11, MXLAT = 52, IPIN = 0 */
    /* TODO: Split next command using pci_set_byte. */
    pci_set_long(pci_conf + PCI_INTERRUPT_LINE, 0x00000100);
    /* Power Management Capabilities */
    pci_set_long(pci_conf + 0x40, 0x7e020001);
    /* Power Management Control and Status */
    //~ pci_set_long(pci_conf + 0x44, 0x00000000);
    /* 0x48...0xff reserved, returns 0 */
}

static NetClientInfo net_info = {
    .type = NET_CLIENT_OPTIONS_KIND_NIC,
    .size = sizeof(NICState),
    .can_receive = nic_can_receive,
    .receive = nic_receive,
    .cleanup = nic_cleanup,
};

static int tnetw1130_init(PCIDevice *pci_dev)
{
    TNETW1130State *d = DO_UPCAST(TNETW1130State, dev, pci_dev);
    tnetw1130_t *s = &d->tnetw1130;

    /* TI TNETW1130 */
    tnetw1130_pci_config(d->dev.config);

    /* Handler for memory-mapped I/O */
    memory_region_init_io(&d->mmio_bar0, &tnetw1130_ops0, s, "tnetw1130_mmio0",
                          TNETW1130_MEM0_SIZE);
    memory_region_init_io(&d->mmio_bar1, &tnetw1130_ops1, s, "tnetw1130_mmio1",
                          TNETW1130_MEM1_SIZE);

    pci_register_bar(&d->dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &d->mmio_bar0);
    pci_register_bar(&d->dev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &d->mmio_bar1);

#if 0
    static const char macaddr[6] = {
        0x00, 0x60, 0x65, 0x02, 0x4a, 0x8e
    };
    memcpy(s->conf.macaddr.a, macaddr, 6);
#endif
    qemu_macaddr_default_if_unset(&s->conf.macaddr);
    tnetw1130_reset(s);

    s->nic = qemu_new_nic(&net_info, &s->conf,
                          object_get_typename(OBJECT(pci_dev)),
                          pci_dev->qdev.id, s);

    qemu_format_nic_info_str(qemu_get_queue(s->nic), s->conf.macaddr.a);

    qemu_register_reset(nic_reset, d);

    return 0;
}

static int pci_tnetw1130_init(PCIDevice* pci_dev)
{
#if defined(DEBUG_TNETW1130)
    set_traceflags("DEBUG_TNETW1130");
#endif
    TRACE(TNETW, logout("\n"));
    return tnetw1130_init(pci_dev);
}

static void pci_tnetw1130_uninit(PCIDevice *pci_dev)
{
    TNETW1130State *s = DO_UPCAST(TNETW1130State, dev, pci_dev);
    memory_region_destroy(&s->mmio_bar0);
    memory_region_destroy(&s->mmio_bar1);
    qemu_del_nic(s->tnetw1130.nic);
}

static const VMStateDescription vmstate_pci_tnetw1130 = {
    .name = "tnetw1130",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField []) {
        VMSTATE_PCI_DEVICE(dev, TNETW1130State),
        // TODO: entries for tnetw1130 are missing here.
        //~ VMSTATE_UINT32(rxbuf_size, E1000State),
        //~ VMSTATE_UINT32(rxbuf_min_shift, E1000State),
        //~ VMSTATE_UINT32(eecd_state.val_in, E1000State),
        //~ VMSTATE_UINT16(eecd_state.bitnum_in, E1000State),
        //~ VMSTATE_UINT16(eecd_state.bitnum_out, E1000State),
        //~ VMSTATE_UINT16(eecd_state.reading, E1000State),
        //~ VMSTATE_UINT32(eecd_state.old_eecd, E1000State),
        //~ VMSTATE_UINT8(tx.ipcss, E1000State),
        //~ VMSTATE_UINT8(tx.ipcso, E1000State),
        //~ VMSTATE_UINT16(tx.ipcse, E1000State),
        //~ VMSTATE_UINT8(tx.tucss, E1000State),
        //~ VMSTATE_UINT8(tx.tucso, E1000State),
        //~ VMSTATE_UINT16(tx.tucse, E1000State),
        //~ VMSTATE_UINT32(tx.paylen, E1000State),
        //~ VMSTATE_UINT8(tx.hdr_len, E1000State),
        //~ VMSTATE_UINT16(tx.mss, E1000State),
        //~ VMSTATE_UINT16(tx.size, E1000State),
        //~ VMSTATE_UINT16(tx.tso_frames, E1000State),
        //~ VMSTATE_UINT8(tx.sum_needed, E1000State),
        //~ VMSTATE_INT8(tx.ip, E1000State),
        //~ VMSTATE_INT8(tx.tcp, E1000State),
        //~ VMSTATE_BUFFER(tx.header, E1000State),
        //~ VMSTATE_BUFFER(tx.data, E1000State),
        //~ VMSTATE_UINT16_ARRAY(eeprom_data, E1000State, 64),
        //~ VMSTATE_UINT16_ARRAY(phy_reg, E1000State, 0x20),
        //~ VMSTATE_UINT32(mac_reg[CTRL], E1000State),
        //~ VMSTATE_UINT32(mac_reg[EECD], E1000State),
        //~ VMSTATE_UINT32(mac_reg[EERD], E1000State),
        //~ VMSTATE_UINT32(mac_reg[GPRC], E1000State),
        //~ VMSTATE_UINT32(mac_reg[GPTC], E1000State),
        //~ VMSTATE_UINT32(mac_reg[ICR], E1000State),
        //~ VMSTATE_UINT32(mac_reg[ICS], E1000State),
        //~ VMSTATE_UINT32(mac_reg[IMC], E1000State),
        //~ VMSTATE_UINT32(mac_reg[IMS], E1000State),
        //~ VMSTATE_UINT32(mac_reg[LEDCTL], E1000State),
        //~ VMSTATE_UINT32(mac_reg[MANC], E1000State),
        //~ VMSTATE_UINT32(mac_reg[MDIC], E1000State),
        //~ VMSTATE_UINT32(mac_reg[MPC], E1000State),
        //~ VMSTATE_UINT32(mac_reg[PBA], E1000State),
        //~ VMSTATE_UINT32(mac_reg[RCTL], E1000State),
        //~ VMSTATE_UINT32(mac_reg[RDBAH], E1000State),
        //~ VMSTATE_UINT32(mac_reg[RDBAL], E1000State),
        //~ VMSTATE_UINT32(mac_reg[RDH], E1000State),
        //~ VMSTATE_UINT32(mac_reg[RDLEN], E1000State),
        //~ VMSTATE_UINT32(mac_reg[RDT], E1000State),
        //~ VMSTATE_UINT32(mac_reg[STATUS], E1000State),
        //~ VMSTATE_UINT32(mac_reg[SWSM], E1000State),
        //~ VMSTATE_UINT32(mac_reg[TCTL], E1000State),
        //~ VMSTATE_UINT32(mac_reg[TDBAH], E1000State),
        //~ VMSTATE_UINT32(mac_reg[TDBAL], E1000State),
        //~ VMSTATE_UINT32(mac_reg[TDH], E1000State),
        //~ VMSTATE_UINT32(mac_reg[TDLEN], E1000State),
        //~ VMSTATE_UINT32(mac_reg[TDT], E1000State),
        //~ VMSTATE_UINT32(mac_reg[TORH], E1000State),
        //~ VMSTATE_UINT32(mac_reg[TORL], E1000State),
        //~ VMSTATE_UINT32(mac_reg[TOTH], E1000State),
        //~ VMSTATE_UINT32(mac_reg[TOTL], E1000State),
        //~ VMSTATE_UINT32(mac_reg[TPR], E1000State),
        //~ VMSTATE_UINT32(mac_reg[TPT], E1000State),
        //~ VMSTATE_UINT32(mac_reg[TXDCTL], E1000State),
        //~ VMSTATE_UINT32(mac_reg[WUFC], E1000State),
        //~ VMSTATE_UINT32(mac_reg[VET], E1000State),
        //~ VMSTATE_UINT32_SUB_ARRAY(mac_reg, E1000State, RA, 32),
        //~ VMSTATE_UINT32_SUB_ARRAY(mac_reg, E1000State, MTA, 128),
        //~ VMSTATE_UINT32_SUB_ARRAY(mac_reg, E1000State, VFTA, 128),
        VMSTATE_END_OF_LIST()
    }
};

static Property tnetw1130_properties[] = {
    DEFINE_NIC_PROPERTIES(TNETW1130State, tnetw1130.conf),
    DEFINE_PROP_END_OF_LIST(),
};

static void tnetw1130_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    dc->desc = "Texas Instruments TNETW1130";
    dc->props = tnetw1130_properties;
    //~ dc->reset = qdev_tnetw1130_reset;
    dc->vmsd = &vmstate_pci_tnetw1130;
    //~ k->romfile = "pxe-tnetw1130.rom";
    k->init = pci_tnetw1130_init;
    k->exit = pci_tnetw1130_uninit;
    k->vendor_id = PCI_VENDOR_ID_TI;
    /* wireless network controller */
    k->class_id = PCI_CLASS_NETWORK_OTHER;
    k->device_id = 0x9066;
    //~ k->revision = 0x01;
    k->subsystem_vendor_id = PCI_VENDOR_ID_TI;
    k->subsystem_id = 0x9067;
}

static const TypeInfo pci_tnetw1130_info = {
    .name = "tnetw1130",
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(TNETW1130State),
    .class_init = tnetw1130_class_init,
};

static void tnetw1130_register_types(void)
{
    type_register_static(&pci_tnetw1130_info);
}

type_init(tnetw1130_register_types)

/*

00:0a.0 Network controller: Texas Instruments ACX 111 54Mbps Wireless Interface
        Subsystem: Abocom Systems Inc: Unknown device ab90
        Flags: bus master, medium devsel, latency 32, IRQ 10
        Memory at dffdc000 (32-bit, non-prefetchable) [size=8K]
        Memory at dffa0000 (32-bit, non-prefetchable) [size=128K]
        Capabilities: [40] Power Management version 2

04:08.0 Network controller: Texas Instruments ACX 111 54Mbps Wireless Interface
        Subsystem: Texas Instruments Unknown device 9067
        Flags: medium devsel, IRQ 50
        Memory at faafe000 (32-bit, non-prefetchable) [size=8K]
        Memory at faac0000 (32-bit, non-prefetchable) [size=128K]
        Capabilities: [40] Power Management version 2

01:08.0 Network controller: Texas Instruments ACX 111 54Mbps Wireless Interface
        Subsystem: Netgear Unknown device 4c00
        Control: I/O+ Mem+ BusMaster+ SpecCycle- MemWINV- VGASnoop- ParErr- Stepping- SERR- FastB2B-
        Status: Cap+ 66MHz- UDF- FastB2B- ParErr- DEVSEL=medium >TAbort- <TAbort- <MAbort- >SERR- <PERR-
        Latency: 32, Cache Line Size: 32 bytes
        Interrupt: pin A routed to IRQ 201
        Region 0: Memory at eb020000 (32-bit, non-prefetchable) [size=8K]
        Region 1: Memory at eb000000 (32-bit, non-prefetchable) [size=128K]
        Capabilities: [40] Power Management version 2
                Flags: PMEClk- DSI- D1+ D2+ AuxCurrent=0mA PME(D0+,D1+,D2+,D3hot+,D3cold-)
                Status: D0 PME-Enable- DSel=0 DScale=0 PME-

00:09.0 Network controller: Texas Instruments ACX 111 54Mbps Wireless Interface
        Subsystem: D-Link System Inc: Unknown device 3b04
        Flags: medium devsel, IRQ 11
        Memory at de020000 (32-bit, non-prefetchable) [size=8K]
        Memory at de000000 (32-bit, non-prefetchable) [size=128K]
        Capabilities: [40] Power Management version 2

0000:02:08.0 Network controller: Texas Instruments ACX 111 54Mbps Wireless Interface
        Subsystem: Abocom Systems Inc: Unknown device ab90
        Flags: bus master, medium devsel, latency 32, IRQ 10
        Memory at f8140000 (32-bit, non-prefetchable) [size=8K]
        Memory at f8100000 (32-bit, non-prefetchable) [size=128K]
        Capabilities: [40] Power Management version 2

02:0d.0 Network controller: Texas Instruments ACX 111 54Mbps Wireless Interface
        Subsystem: D-Link System Inc Unknown device 3b04
        Flags: bus master, medium devsel, latency 64, IRQ 10
        Memory at feafc000 (32-bit, non-prefetchable) [size=8K]
        Memory at feac0000 (32-bit, non-prefetchable) [size=128K]
        Capabilities: [40] Power Management version 2

#define AVALANCHE_VLYNQ0_MEM1_BASE      0x04000000      VLYNQ 0 memory mapped
#define AVALANCHE_VLYNQ0_MEM2_BASE      0x04022000      VLYNQ 0 memory mapped
    uint32_t vlynq0mem1[8 * KiB / 4];        // 0x04000000
    uint32_t vlynq0mem2[128 * KiB / 4];      // 0x04022000
    vlynq0mem2 + 0x0001e0f0            Mailbox (from ACX111)?
    vlynq0mem2 + 0x0001e108            Command (to ACX111)?

	write_reg32(adev, IO_ACX_EEPROM_CFG, 0);
	write_reg32(adev, IO_ACX_EEPROM_ADDR, addr);
	write_flush(adev);
	write_reg32(adev, IO_ACX_EEPROM_CTL, 2);
	while (read_reg16(adev, IO_ACX_EEPROM_CTL)) {
	}

	*charbuf = read_reg8(adev, IO_ACX_EEPROM_DATA);

ACX111 PCI control block

0xa4041000 / 0x00: 0x9066104c
0xa4041004 / 0x04: 0x02100000
0xa4041008 / 0x08: 0x02800000
0xa404100c / 0x0c: 0x00000000
0xa4041010 / 0x10: 0x00000000
0xa4041014 / 0x14: 0x00000000
0xa4041018 / 0x18: 0x00000000
0xa404101c / 0x1c: 0x00000000
0xa4041020 / 0x20: 0x00000000
0xa4041024 / 0x24: 0x00000000
0xa4041028 / 0x28: 0x00001c02
0xa404102c / 0x2c: 0x9067104c
0xa4041030 / 0x30: 0x00000000
0xa4041034 / 0x34: 0x00000040
0xa4041038 / 0x38: 0x00000000
0xa404103c / 0x3c: 0x00000100
0xa4041040 / 0x40: 0x7e020001

//~ lspci -x: 00: 4c 10 66 90 07 01 10 02 00 00 80 02 04 40 00 00
//~ lspci -x: 10: 00 c0 af fe 00 00 ac fe 00 00 00 00 00 00 00 00
//~ lspci -x: 20: 00 00 00 00 00 00 00 00 02 1c 00 00 86 11 04 3b
//~ lspci -x: 30: 00 00 00 00 40 00 00 00 00 00 00 00 0a 01 00 00

vlynq.c
A1			{ .size = 0x22000, .offset = 0xf0000000 },
			{ .size = 0x40000, .offset = 0xc0000000 },
A2			{ .size = 0x40000, .offset = 0xc0000000 },
			{ .size = 0x22000, .offset = 0xf0000000 },

u32 vlynq_get_mapped(struct vlynq_device *dev, int res)
B1                      vertauscht
B2                      nicht vertauscht

config_read
C1                      vertauscht
C2                      nicht vertauscht

config_write
D1                      vertauscht
D2                      nicht vertauscht


A1 B2 C2 D2:

*/

/* eof */
