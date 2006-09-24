/*
 * QEMU i82559 (EEPRO100) emulation
 *
 * Copyright (c) 2006 Stefan Weil
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <assert.h>
#include "vl.h"

#define PCI_VENDOR_ID           0x00    /* 16 bits */
#define PCI_DEVICE_ID           0x02    /* 16 bits */
#define PCI_COMMAND             0x04    /* 16 bits */

#define PCI_REVISION            0x08    /* 8 bits  */
#define PCI_CLASS_CODE          0x0b    /* 8 bits */
#define PCI_SUBCLASS_CODE       0x0a    /* 8 bits */
#define PCI_HEADER_TYPE         0x0e    /* 8 bits */

#define PCI_BASE_ADDRESS_0      0x10    /* 32 bits */
#define PCI_BASE_ADDRESS_1      0x14    /* 32 bits */
#define PCI_BASE_ADDRESS_2      0x18    /* 32 bits */
#define PCI_BASE_ADDRESS_3      0x1c    /* 32 bits */
#define PCI_BASE_ADDRESS_4      0x20    /* 32 bits */
#define PCI_BASE_ADDRESS_5      0x24    /* 32 bits */

/* debug EEPRO100 card */
#define DEBUG_EEPRO100

#ifdef DEBUG_EEPRO100
#define logout(fmt, args...) printf("EEPRO100 %-24s" fmt, __func__, ##args)
#else
#define logout(fmt, args...) ((void)0)
#endif

#define MAX_ETH_FRAME_SIZE 1514

#define EEPRO100_PMEM_SIZE    (32*1024)
#define EEPRO100_PMEM_START   (16*1024)
#define EEPRO100_PMEM_END     (EEPRO100_PMEM_SIZE+EEPRO100_PMEM_START)
#define EEPRO100_MEM_SIZE     EEPRO100_PMEM_END

#define KiB 1024

#define PCI_MEM_SIZE            (4 * KiB)
#define PCI_IO_SIZE             64
#define PCI_FLASH_SIZE          (128 * KiB)



/* Emulation of 9346 EEPROM (64 * 16 bit) */

#define EEPROM_9346_ADDR_BITS 6
#define EEPROM_9346_SIZE  (1 << EEPROM_9346_ADDR_BITS)
#define EEPROM_9346_ADDR_MASK (EEPROM_9346_SIZE - 1)

#define SET_MASKED(input, mask, curr) \
    ( ( (input) & ~(mask) ) | ( (curr) & (mask) ) )

#if 0
#define EEPROM_CS       0x08
#define EEPROM_SK       0x04
#define EEPROM_DI       0x02
#define EEPROM_DO       0x01
#else
#define EEPROM_CS       0x02
#define EEPROM_SK       0x01
#define EEPROM_DI       0x04
#define EEPROM_DO       0x08
#endif

typedef enum {
    Chip9346_op_mask = 0xc0,          /* 10 zzzzzz */
    Chip9346_op_read = 0x80,          /* 10 AAAAAA */
    Chip9346_op_write = 0x40,         /* 01 AAAAAA D(15)..D(0) */
    Chip9346_op_ext_mask = 0xf0,      /* 11 zzzzzz */
    Chip9346_op_write_enable = 0x30,  /* 00 11zzzz */
    Chip9346_op_write_all = 0x10,     /* 00 01zzzz */
    Chip9346_op_write_disable = 0x00, /* 00 00zzzz */
} Chip9346Operation;

typedef enum {
    Chip9346_none = 0,
    Chip9346_enter_command_mode,
    Chip9346_read_command,
    Chip9346_data_read,      /* from output register */
    Chip9346_data_write,     /* to input register, then to contents at specified address */
    Chip9346_data_write_all, /* to input register, then filling contents */
} Chip9346Mode;

typedef struct {
    uint16_t contents[EEPROM_9346_SIZE];
    Chip9346Mode mode;
    uint32_t tick;
    uint8_t  address;
    uint16_t input;
    uint16_t output;

    uint8_t eecs;
    uint8_t eesk;
    uint8_t eedi;
    uint8_t eedo;

    uint8_t Cfg9346;
} EEprom9346;

static void eeprom_decode_command(EEprom9346 *eeprom, uint8_t command)
{
    logout("eeprom command 0x%02x\n", command);

    switch (command & Chip9346_op_mask) {
        case Chip9346_op_read:
        {
            eeprom->address = command & EEPROM_9346_ADDR_MASK;
            eeprom->output = eeprom->contents[eeprom->address];
            eeprom->eedo = 0;
            eeprom->tick = 0;
            eeprom->mode = Chip9346_data_read;
            logout("eeprom read from address 0x%02x data=0x%04x\n",
                   eeprom->address, eeprom->output);
        }
        break;

        case Chip9346_op_write:
        {
            eeprom->address = command & EEPROM_9346_ADDR_MASK;
            eeprom->input = 0;
            eeprom->tick = 0;
            eeprom->mode = Chip9346_none; /* Chip9346_data_write */
            logout("eeprom begin write to address 0x%02x\n",
                   eeprom->address);
        }
        break;
        default:
            eeprom->mode = Chip9346_none;
            switch (command & Chip9346_op_ext_mask) {
                case Chip9346_op_write_enable:
                    logout("eeprom write enabled\n");
                    break;
                case Chip9346_op_write_all:
                    logout("eeprom begin write all\n");
                    break;
                case Chip9346_op_write_disable:
                    logout("eeprom write disabled\n");
                    break;
            }
            break;
    }
}

static void prom9346_shift_clock(EEprom9346 *eeprom)
{
    int bit = eeprom->eedi?1:0;

    ++ eeprom->tick;

    logout("tick %d eedi=%d eedo=%d\n", eeprom->tick, eeprom->eedi, eeprom->eedo);

    switch (eeprom->mode) {
        case Chip9346_enter_command_mode:
            if (bit) {
                eeprom->mode = Chip9346_read_command;
                eeprom->tick = 0;
                eeprom->input = 0;
                logout("+++ synchronized, begin command read\n");
            }
            break;

        case Chip9346_read_command:
            eeprom->input = (eeprom->input << 1) | (bit & 1);
            if (eeprom->tick == 8) {
                eeprom_decode_command(eeprom, eeprom->input & 0xff);
            }
            break;

        case Chip9346_data_read:
            eeprom->eedo = (eeprom->output & 0x8000)?1:0;
            eeprom->output <<= 1;
            if (eeprom->tick == 16) {
#if 1
        // the FreeBSD drivers (rl and re) don't explicitly toggle
        // CS between reads (or does setting Cfg9346 to 0 count too?),
        // so we need to enter wait-for-command state here
                eeprom->mode = Chip9346_enter_command_mode;
                eeprom->input = 0;
                eeprom->tick = 0;

                logout("+++ end of read, awaiting next command\n");
#else
        // original behaviour
                ++eeprom->address;
                eeprom->address &= EEPROM_9346_ADDR_MASK;
                eeprom->output = eeprom->contents[eeprom->address];
                eeprom->tick = 0;

                logout("read next address 0x%02x data=0x%04x\n",
                       eeprom->address, eeprom->output);
#endif
            }
            break;

        case Chip9346_data_write:
            eeprom->input = (eeprom->input << 1) | (bit & 1);
            if (eeprom->tick == 16) {
                logout("eeprom write to address 0x%02x data=0x%04x\n",
                       eeprom->address, eeprom->input);

                eeprom->contents[eeprom->address] = eeprom->input;
                eeprom->mode = Chip9346_none; /* waiting for next command after CS cycle */
                eeprom->tick = 0;
                eeprom->input = 0;
            }
            break;

        case Chip9346_data_write_all:
            eeprom->input = (eeprom->input << 1) | (bit & 1);
            if (eeprom->tick == 16) {
                int i;
                for (i = 0; i < EEPROM_9346_SIZE; i++) {
                    eeprom->contents[i] = eeprom->input;
                }
                logout("eeprom filled with data=0x%04x\n",
                       eeprom->input);

                eeprom->mode = Chip9346_enter_command_mode;
                eeprom->tick = 0;
                eeprom->input = 0;
            }
            break;

        default:
            break;
    }
}

static int prom9346_get_wire(EEprom9346 *eeprom)
{
    if (!eeprom->eecs)
        return 0;

    return eeprom->eedo;
}

static void prom9346_set_wire(EEprom9346 *eeprom, int eecs, int eesk, int eedi)
{
    uint8_t old_eecs = eeprom->eecs;
    uint8_t old_eesk = eeprom->eesk;

    eeprom->eecs = eecs;
    eeprom->eesk = eesk;
    eeprom->eedi = eedi;

    logout("+++ wires CS=%d SK=%d DI=%d DO=%d\n",
           eeprom->eecs, eeprom->eesk, eeprom->eedi, eeprom->eedo);

    if (!old_eecs && eecs) {
        /* Synchronize start */
        eeprom->tick = 0;
        eeprom->input = 0;
        eeprom->output = 0;
        eeprom->mode = Chip9346_enter_command_mode;

        logout("begin access, enter command mode\n");
    }

    if (!eecs) {
        logout("end access\n");
        return;
    }

    if (!old_eesk && eesk) {
        /* SK front rules */
        prom9346_shift_clock(eeprom);
    }
}

static void Cfg9346_write(EEprom9346 *eeprom, uint32_t val)
{
    val &= 0xff;

    logout("Cfg9346 write val=0x%02x\n", val);

    /* mask unwriteable bits */
    //~ val = SET_MASKED(val, 0x31, eeprom->Cfg9346);

    int eecs = ((val & EEPROM_CS) != 0);
    int eesk = ((val & EEPROM_SK) != 0);
    int eedi = ((val & EEPROM_DI) != 0);
    prom9346_set_wire(eeprom, eecs, eesk, eedi);

    eeprom->Cfg9346 = val;
}

static uint32_t Cfg9346_read(EEprom9346 *eeprom)
{
    uint32_t ret = eeprom->Cfg9346;

    int eedo = prom9346_get_wire(eeprom);
    if (eedo) {
        ret |=  EEPROM_DO;
    } else {
        ret &= ~EEPROM_DO;
    }

    logout("Cfg9346 read val=0x%02x\n", ret);

    return ret;
}









typedef struct {
    uint8_t cmd;
    uint32_t start;
    uint32_t stop;
    uint8_t boundary;
    uint8_t tsr;
    uint8_t tpsr;
    uint16_t tcnt;
    uint16_t rcnt;
    uint32_t rsar;
    uint8_t rsr;
    uint8_t rxcr;
    uint8_t isr;
    uint8_t dcfg;
    uint8_t imr;
    uint8_t phys[6]; /* mac address */
    uint8_t curpag;
    uint8_t mult[8]; /* multicast mask array */
    int irq;
    int mmio_index;
    uint32_t region[3]; /* PCI region addresses */
    PCIDevice *pci_dev;
    VLANClientState *vc;
    uint8_t macaddr[6];
    uint8_t mem[EEPRO100_MEM_SIZE];
    EEprom9346 eeprom;
    uint32_t pointer;
    uint32_t rxaddr;
    uint32_t statsaddr;
    uint16_t status;
    unsigned scb_m:1;
} EEPRO100State;

static void nic_reset(void *opaque);

static void eepro100_update_irq(EEPRO100State *s)
{
    int isr;
    isr = (s->isr & s->imr) & 0x7f;
    logout("Set IRQ line %d to %d (%02x %02x)\n",
           s->irq, isr ? 1 : 0, s->isr, s->imr);
    if (s->irq == 16) {
        /* PCI irq */
        pci_set_irq(s->pci_dev, 0, (isr != 0));
    } else {
        /* ISA irq */
        pic_set_irq(s->irq, (isr != 0));
    }
}

#define POLYNOMIAL 0x04c11db6

/* From FreeBSD */
/* XXX: optimize */
static int compute_mcast_idx(const uint8_t *ep)
{
    uint32_t crc;
    int carry, i, j;
    uint8_t b;

    crc = 0xffffffff;
    for (i = 0; i < 6; i++) {
        b = *ep++;
        for (j = 0; j < 8; j++) {
            carry = ((crc & 0x80000000L) ? 1 : 0) ^ (b & 0x01);
            crc <<= 1;
            b >>= 1;
            if (carry)
                crc = ((crc ^ POLYNOMIAL) | carry);
        }
    }
    return (crc >> 26);
}

static int eepro100_buffer_full(EEPRO100State *s)
{
    int avail, index, boundary;

    index = s->curpag << 8;
    boundary = s->boundary << 8;
    if (index <= boundary)
        avail = boundary - index;
    else
        avail = (s->stop - s->start) - (index - boundary);
    if (avail < (MAX_ETH_FRAME_SIZE + 4))
        return 1;
    return 0;
}

static int eepro100_can_receive(void *opaque)
{
    EEPRO100State *s = opaque;
    logout("%p\n", s);
    return !eepro100_buffer_full(s);
}

#define MIN_BUF_SIZE 60

static void eepro100_receive(void *opaque, const uint8_t *buf, int size)
{
    EEPRO100State *s = opaque;
    uint8_t *p;
    int total_len, next, avail, len, index, mcast_idx;
    uint8_t buf1[60];
    static const uint8_t broadcast_macaddr[6] =
        { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

    logout("%p received len=%d\n", s, size);

    if (eepro100_buffer_full(s))
        return;

    /* XXX: check this */
    if (s->rxcr & 0x10) {
        /* promiscuous: receive all */
    } else {
        if (!memcmp(buf,  broadcast_macaddr, 6)) {
            /* broadcast address */
            if (!(s->rxcr & 0x04))
                return;
        } else if (buf[0] & 0x01) {
            /* multicast */
            if (!(s->rxcr & 0x08))
                return;
            mcast_idx = compute_mcast_idx(buf);
            if (!(s->mult[mcast_idx >> 3] & (1 << (mcast_idx & 7))))
                return;
        } else if (s->mem[0] == buf[0] &&
                   s->mem[2] == buf[1] &&
                   s->mem[4] == buf[2] &&
                   s->mem[6] == buf[3] &&
                   s->mem[8] == buf[4] &&
                   s->mem[10] == buf[5]) {
            /* match */
        } else {
            return;
        }
    }


    /* if too small buffer, then expand it */
    if (size < MIN_BUF_SIZE) {
        memcpy(buf1, buf, size);
        memset(buf1 + size, 0, MIN_BUF_SIZE - size);
        buf = buf1;
        size = MIN_BUF_SIZE;
    }

    index = s->curpag << 8;
    /* 4 bytes for header */
    total_len = size + 4;
    /* address for next packet (4 bytes for CRC) */
    next = index + ((total_len + 4 + 255) & ~0xff);
    if (next >= s->stop)
        next -= (s->stop - s->start);
    /* prepare packet header */
    p = s->mem + index;
    //~ s->rsr = ENRSR_RXOK; /* receive status */
    /* XXX: check this */
    //~ if (buf[0] & 0x01)
        //~ s->rsr |= ENRSR_PHY;
    p[0] = s->rsr;
    p[1] = next >> 8;
    p[2] = total_len;
    p[3] = total_len >> 8;
    index += 4;

    /* write packet data */
    while (size > 0) {
        avail = s->stop - index;
        len = size;
        if (len > avail)
            len = avail;
        memcpy(s->mem + index, buf, len);
        buf += len;
        index += len;
        if (index == s->stop)
            index = s->start;
        size -= len;
    }
    s->curpag = next >> 8;

    /* now we can signal we have receive something */
    //~ s->isr |= ENISR_RX;
    eepro100_update_irq(s);
}

static const char *reg[PCI_IO_SIZE / 4] = {
  "Command/Status",
  "General Pointer",
  "Port",
  "EPROM/Flash Control",
  "MDI Control",
  "Receive DMA Byte Count",
};

static char *regname(uint32_t addr)
{
  static char buf[16];
  if (addr < PCI_IO_SIZE) {
    const char *r = reg[addr / 4];
    if (r != 0) {
      sprintf(buf, "%s+%u", r, addr % 4);
    } else {
      sprintf(buf, "0x%02x", addr);
    }
  } else {
    sprintf(buf, "??? 0x%08x", addr);
  }
  return buf;
}

static uint16_t eepro100_read_status(EEPRO100State *s)
{
    uint16_t val = s->status;
    logout("val=0x%04x\n", val);
    return val;
}

static void eepro100_write_status(EEPRO100State *s, uint16_t val)
{
    logout("val=0x%04x\n", val);
    s->status = val;
}

static uint16_t eepro100_read_command(EEPRO100State *s)
{
    uint16_t val = 0xffff;
    //~ logout("val=0x%04x\n", val);
    return val;
}

static void eepro100_write_command(EEPRO100State *s, uint16_t val)
{
    switch (val & 0xff) {
        case 0x01:      /* RX_START */
            s->scb_m = ((val & 0x100) != 0);
            //~ s->rxaddr = s->pointer;
            logout("val=0x%04x (rx start)\n", val);
            break;
        case 0x06:
            s->scb_m = ((val & 0x100) != 0);
            s->rxaddr = s->pointer;
            logout("val=0x%04x\n", val);
            break;
        case 0x10:      /* CU_START */
            s->scb_m = ((val & 0x100) != 0);
            //~ s->rxaddr = s->pointer;
            logout("val=0x%04x (cu start)\n", val);
            break;
        case 0x40:
            s->scb_m = ((val & 0x100) != 0);
            s->statsaddr = s->pointer;
            logout("val=0x%04x\n", val);
            break;
        case 0x60:      /* CU_CMD_BASE */
            s->scb_m = ((val & 0x100) != 0);
            //~ s->rxaddr = s->pointer;
            logout("val=0x%04x\n", val);
            break;
        default:
            logout("val=0x%04x\n", val);
    }
}

static void eepro100_write_pointer(EEPRO100State *s, uint32_t val)
{
    s->pointer = val;
     logout("val=0x%08x\n", val);
}

static uint32_t eepro100_read_mdi(EEPRO100State *s)
{
    uint32_t val = 0xffffffff;
    logout("val=0x%08x\n", val);
    return val;
}

static void eepro100_write_mdi(EEPRO100State *s, uint32_t val)
{
    logout("val=0x%08x\n", val);
}

#define PORT_SOFTWARE_RESET     0
#define PORT_SELFTEST           1
#define PORT_SELECTIVE_RESET    2
#define PORT_DUMP               3
#define PORT_SELECTION_MASK     3

typedef struct {
    uint32_t st_sign;   /* Self Test Signature */
    uint32_t st_result; /* Self Test Results */
} eepro100_selftest_t __attribute__ ((__packed__));

static void eepro100_write_port(EEPRO100State *s, uint32_t val)
{
    uint32_t address = (val & ~PORT_SELECTION_MASK);
    uint8_t  selection = (val & PORT_SELECTION_MASK);
    switch (selection) {
        case PORT_SOFTWARE_RESET:
            nic_reset(s);
            break;
        case PORT_SELFTEST:
            logout("selftest address=0x%08x\n", address);
            eepro100_selftest_t data;
            cpu_physical_memory_read(address, (uint8_t *)&data, sizeof(data));
            data.st_sign = 0xffffffff;
            data.st_result = 0;
            cpu_physical_memory_write(address, (uint8_t *)&data, sizeof(data));
            break;
        default:
            logout("val=0x%08x (unimplemented)\n", val);
    }
}

static uint8_t eepro100_read1(EEPRO100State *s, uint32_t addr)
{
    uint8_t ret = 0xff;

    switch (addr) {
        case 0x02:
            ret = eepro100_read_command(s);
            break;
        case 0x1b:      /* power management driver register */
            ret = 0;
            break;
        case 0x1d:      /* general status register */
            /* 100 Mbps full duplex */
            ret = 0x03;
            break;
        default:
            logout("addr=%s val=%02x\n", regname(addr), ret);
    }
    return ret;
}

static uint16_t eepro100_read2(EEPRO100State *s, uint32_t addr)
{
    uint16_t ret = 0xffff;

    switch (addr) {
        case 0x00:
            ret = eepro100_read_status(s);
            break;
        case 0x0e:
            ret = Cfg9346_read(&s->eeprom);
            break;
        default:
            logout("addr=%s val=%04x\n", regname(addr), ret);
    }
    return ret;
}

static uint32_t eepro100_read4(EEPRO100State *s, uint32_t addr)
{
    int ret = 0xffffffff;
    switch (addr) {
        case 0x10:
            ret = eepro100_read_mdi(s);
            break;
        default:
            logout("addr=%s val=%08x\n", regname(addr), ret);
    }
    return ret;
}


static void eepro100_write1(EEPRO100State *s, uint32_t addr, uint8_t val)
{
    logout("addr=%s val=0x%02x\n", regname(addr), val);
}

static void eepro100_write2(EEPRO100State *s, uint32_t addr, uint16_t val)
{
    switch (addr) {
        case 0x00:
            eepro100_write_status(s, val);
            break;
        case 0x02:
            eepro100_write_command(s, val);
            break;
        case 0x0e:
            Cfg9346_write(&s->eeprom, val);
            break;
        default:
            logout("addr=%s val=0x%04x\n", regname(addr), val);
    }
}

static void eepro100_write4(EEPRO100State *s, uint32_t addr, uint32_t val)
{
    switch (addr) {
        case 0x04:
            eepro100_write_pointer(s, val);
            break;
        case 0x10:
            eepro100_write_mdi(s, val);
            break;
        case 0x08:
            eepro100_write_port(s, val);
            break;
        default:
            logout("addr=%s val=0x%08x\n", regname(addr), val);
    }
}

static uint32_t ioport_read1(void *opaque, uint32_t addr)
{
    EEPRO100State *s = opaque;
    return eepro100_read1(s, addr - s->region[1]);
}

static uint32_t ioport_read2(void *opaque, uint32_t addr)
{
    EEPRO100State *s = opaque;
    return eepro100_read2(s, addr - s->region[1]);
}

static uint32_t ioport_read4(void *opaque, uint32_t addr)
{
    EEPRO100State *s = opaque;
    return eepro100_read4(s, addr - s->region[1]);
}

static void ioport_write1(void *opaque, uint32_t addr, uint32_t val)
{
    EEPRO100State *s = opaque;
    eepro100_write1(s, addr - s->region[1], val);
}

static void ioport_write2(void *opaque, uint32_t addr, uint32_t val)
{
    EEPRO100State *s = opaque;
    eepro100_write2(s, addr - s->region[1], val);
}

static void ioport_write4(void *opaque, uint32_t addr, uint32_t val)
{
    EEPRO100State *s = opaque;
    eepro100_write4(s, addr - s->region[1], val);
}

static void nic_save(QEMUFile* f,void* opaque)
{
        EEPRO100State* s=(EEPRO100State*)opaque;

        if (s->pci_dev)
            pci_device_save(s->pci_dev, f);

        qemu_put_8s(f, &s->rxcr);

        qemu_put_8s(f, &s->cmd);
        qemu_put_be32s(f, &s->start);
        qemu_put_be32s(f, &s->stop);
        qemu_put_8s(f, &s->boundary);
        qemu_put_8s(f, &s->tsr);
        qemu_put_8s(f, &s->tpsr);
        qemu_put_be16s(f, &s->tcnt);
        qemu_put_be16s(f, &s->rcnt);
        qemu_put_be32s(f, &s->rsar);
        qemu_put_8s(f, &s->rsr);
        qemu_put_8s(f, &s->isr);
        qemu_put_8s(f, &s->dcfg);
        qemu_put_8s(f, &s->imr);
        qemu_put_buffer(f, s->phys, 6);
        qemu_put_8s(f, &s->curpag);
        qemu_put_buffer(f, s->mult, 8);
        qemu_put_be32s(f, &s->irq);
        qemu_put_buffer(f, s->mem, EEPRO100_MEM_SIZE);
}

static int nic_load(QEMUFile* f,void* opaque,int version_id)
{
        EEPRO100State* s=(EEPRO100State*)opaque;
        int ret;

        if (version_id > 3)
            return -EINVAL;

        if (s->pci_dev && version_id >= 3) {
            ret = pci_device_load(s->pci_dev, f);
            if (ret < 0)
                return ret;
        }

        if (version_id >= 2) {
            qemu_get_8s(f, &s->rxcr);
        } else {
            s->rxcr = 0x0c;
        }

        qemu_get_8s(f, &s->cmd);
        qemu_get_be32s(f, &s->start);
        qemu_get_be32s(f, &s->stop);
        qemu_get_8s(f, &s->boundary);
        qemu_get_8s(f, &s->tsr);
        qemu_get_8s(f, &s->tpsr);
        qemu_get_be16s(f, &s->tcnt);
        qemu_get_be16s(f, &s->rcnt);
        qemu_get_be32s(f, &s->rsar);
        qemu_get_8s(f, &s->rsr);
        qemu_get_8s(f, &s->isr);
        qemu_get_8s(f, &s->dcfg);
        qemu_get_8s(f, &s->imr);
        qemu_get_buffer(f, s->phys, 6);
        qemu_get_8s(f, &s->curpag);
        qemu_get_buffer(f, s->mult, 8);
        qemu_get_be32s(f, &s->irq);
        qemu_get_buffer(f, s->mem, EEPRO100_MEM_SIZE);

        return 0;
}

/***********************************************************/
/* PCI EEPRO100 definitions */

typedef struct PCIEEPRO100State {
    PCIDevice dev;
    EEPRO100State eepro100;
} PCIEEPRO100State;

static void pci_map(PCIDevice *pci_dev, int region_num,
                       uint32_t addr, uint32_t size, int type)
{
    PCIEEPRO100State *d = (PCIEEPRO100State *)pci_dev;
    EEPRO100State *s = &d->eepro100;

    logout("region %d, addr=0x%08x, size=0x%08x, type=%d\n",
           region_num, addr, size, type);

    assert(region_num == 1);
    register_ioport_write(addr, size, 1, ioport_write1, s);
    register_ioport_read(addr, size, 1, ioport_read1, s);
    register_ioport_write(addr, size, 2, ioport_write2, s);
    register_ioport_read(addr, size, 2, ioport_read2, s);
    register_ioport_write(addr, size, 4, ioport_write4, s);
    register_ioport_read(addr, size, 4, ioport_read4, s);

    s->region[region_num] = addr;
}

static void pci_mmio_writeb(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    EEPRO100State *s = opaque;
    addr -= s->region[0];
    //~ logout("addr=%s val=0x%02x\n", regname(addr), val);
    eepro100_write1(s, addr, val);
}

static void pci_mmio_writew(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    EEPRO100State *s = opaque;
    addr -= s->region[0];
    //~ logout("addr=%s val=0x%02x\n", regname(addr), val);
    eepro100_write2(s, addr, val);
}

static void pci_mmio_writel(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    EEPRO100State *s = opaque;
    addr -= s->region[0];
    //~ logout("addr=%s val=0x%02x\n", regname(addr), val);
    eepro100_write4(s, addr, val);
}

static uint32_t pci_mmio_readb(void *opaque, target_phys_addr_t addr)
{
    EEPRO100State *s = opaque;
    addr -= s->region[0];
    //~ logout("addr=%s\n", regname(addr));
    return eepro100_read1(s, addr);
}

static uint32_t pci_mmio_readw(void *opaque, target_phys_addr_t addr)
{
    EEPRO100State *s = opaque;
    addr -= s->region[0];
    //~ logout("addr=%s\n", regname(addr));
    return eepro100_read2(s, addr);
}

static uint32_t pci_mmio_readl(void *opaque, target_phys_addr_t addr)
{
    EEPRO100State *s = opaque;
    addr -= s->region[0];
    //~ logout("addr=%s\n", regname(addr));
    return eepro100_read4(s, addr);
}

static CPUWriteMemoryFunc *pci_mmio_write[] = {
    pci_mmio_writeb,
    pci_mmio_writew,
    pci_mmio_writel
};

static CPUReadMemoryFunc *pci_mmio_read[] = {
    pci_mmio_readb,
    pci_mmio_readw,
    pci_mmio_readl
};

static void pci_mmio_map(PCIDevice *pci_dev, int region_num,
                            uint32_t addr, uint32_t size, int type)
{
    PCIEEPRO100State *d = (PCIEEPRO100State *)pci_dev;

    logout("region %d, addr=0x%08x, size=0x%08x, type=%d\n",
           region_num, addr, size, type);

    if (region_num == 0) {
        /* Map control / status registers. */
        cpu_register_physical_memory(addr, size, d->eepro100.mmio_index);
        d->eepro100.region[region_num] = addr;
    }
}

static void nic_reset(void *opaque)
{
    EEPRO100State *s = (EEPRO100State *)opaque;
    logout("%p\n", s);
    /* prepare eeprom */
    size_t i;
    uint16_t sum = 0;
    memcpy(&s->eeprom.contents[0], s->macaddr, 6);
    for (i = 0; i < 6; i++) {
        sum += s->eeprom.contents[i];
    }
    for (i = 6; i < EEPROM_9346_SIZE - 1; i++) {
        s->eeprom.contents[i] = i;
        sum += i;
    }
    s->eeprom.contents[EEPROM_9346_SIZE - 1] = 0xbaba - sum;
}

void pci_eepro100_init(PCIBus *bus, NICInfo *nd)
{
    PCIEEPRO100State *d;
    EEPRO100State *s;
    uint8_t *pci_conf;

    logout("\n");

    d = (PCIEEPRO100State *)pci_register_device(bus, "EEPRO100",
        sizeof(PCIEEPRO100State), -1, NULL, NULL);
    pci_conf = d->dev.config;
#define PCI_CONFIG_8(offset, value) \
    (pci_conf[offset] = (value))
#define PCI_CONFIG_16(offset, value) \
    (*(uint16_t *)&pci_conf[offset] = cpu_to_le16(value))
#define PCI_CONFIG_32(offset, value) \
    (*(uint32_t *)&pci_conf[offset] = cpu_to_le32(value))
    /* PCI Vendor ID */
    PCI_CONFIG_16(PCI_VENDOR_ID, 0x8086);
    /* PCI Device ID */
    PCI_CONFIG_16(PCI_DEVICE_ID, 0x1209);
    /* PCI Command */
    PCI_CONFIG_16(PCI_COMMAND, 0x0000);
    /* PCI Status */
    PCI_CONFIG_16(0x06, 0x2800);
    /* PCI Class code / PCI Revision ID */
    PCI_CONFIG_8(PCI_REVISION, 0x08);
    PCI_CONFIG_8(0x09, 0x00);
    PCI_CONFIG_8(PCI_SUBCLASS_CODE, 0x00); // ethernet network controller
    PCI_CONFIG_8(PCI_CLASS_CODE, 0x02); // network controller
    /* bist / PCI Hader Type / PCI Latency Timer / PCI Cache Line Size */
    /* check cache line size!!! */
    //~ PCI_CONFIG_8(0x0c, 0x00);
    PCI_CONFIG_8(0x0d, 0x20); // latency timer = 32 clocks
    /* CSR Memory Mapped Base Address */
    PCI_CONFIG_32(PCI_BASE_ADDRESS_0, 0x00000000);
    /* CSR I/O Mapped Base Address */
    PCI_CONFIG_32(PCI_BASE_ADDRESS_1, 0x00000001);
    /* Flash Memory Mapped Base Address */
    PCI_CONFIG_32(PCI_BASE_ADDRESS_2, 0xfffe0000);
    /* Expansion ROM Base Address (depends on boot disable!!!) */
    PCI_CONFIG_32(0x30, 0x00000000);
    /* Capability Pointer */
    PCI_CONFIG_8(0x34, 0xdc);
    /* Interrupt Pin */
    PCI_CONFIG_8(0x3d, 1); // interrupt pin 0
    PCI_CONFIG_8(0x3e, 0x08); // minimum grant
    PCI_CONFIG_8(0x3f, 0x18); // maximum latency
    /* Power Management Capabilities / Next Item Pointer / Capability ID */
    PCI_CONFIG_32(0xdc, 0x7e210001);

    s = &d->eepro100;

    /* Handler for memory-mapped I/O */
    d->eepro100.mmio_index =
      cpu_register_io_memory(0, pci_mmio_read, pci_mmio_write, s);

    pci_register_io_region(&d->dev, 0, PCI_MEM_SIZE,
                           PCI_ADDRESS_SPACE_MEM, pci_mmio_map);
    pci_register_io_region(&d->dev, 1, PCI_IO_SIZE,
                           PCI_ADDRESS_SPACE_IO, pci_map);
    pci_register_io_region(&d->dev, 2, PCI_FLASH_SIZE,
                           PCI_ADDRESS_SPACE_MEM, pci_mmio_map);

    s->irq = 16; // PCI interrupt
    s->pci_dev = &d->dev;
    memcpy(s->macaddr, nd->macaddr, 6);
    assert(s->region[1] == 0);

    nic_reset(s);

    s->vc = qemu_new_vlan_client(nd->vlan, eepro100_receive,
                                 eepro100_can_receive, s);

    snprintf(s->vc->info_str, sizeof(s->vc->info_str),
             "eepro100 pci macaddr=%02x:%02x:%02x:%02x:%02x:%02x",
             s->macaddr[0],
             s->macaddr[1],
             s->macaddr[2],
             s->macaddr[3],
             s->macaddr[4],
             s->macaddr[5]);

    qemu_register_reset(nic_reset, s);

    /* XXX: instance number ? */
    register_savevm("eepro100", 0, 3, nic_save, nic_load, s);
}
