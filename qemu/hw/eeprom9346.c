/*
 * QEMU EEPROM emulation
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

/* Emulation for serial EEPROMs:
 * NMC9306 256-Bit (16 x 16)
 * FM93C46 1024-Bit (256 x 16)
 *
 * Other drivers use these interface functions:
 * eeprom9346_new   - add a new EEPROM (with 16 or 256 words)
 * eeprom9346_reset - reset the EEPROM
 * eeprom9346_read  - read data from the EEPROM
 * eeprom9346_write - write data to the EEPROM
 */

/* Debug EEPROM emulation. */
//~ #define DEBUG_EEPROM

#ifdef DEBUG_EEPROM
#define logout(fmt, args...) printf("EEPROM\t%-24s" fmt, __func__, ##args)
#else
#define logout(fmt, args...) ((void)0)
#endif

#if 0
typedef enum {
  eeprom_read  = 0x80,   /* read register xx */
  eeprom_write = 0x40,   /* write register xx */
  eeprom_erase = 0xc0,   /* erase register xx */
  eeprom_ewen  = 0x30,   /* erase / write enable */
  eeprom_ewds  = 0x00,   /* erase / write disable */
  eeprom_eral  = 0x20,   /* erase all registers */
  eeprom_wral  = 0x10,   /* write all registers */
  eeprom_amask = 0x0f,
  eeprom_imask = 0xf0
} eeprom_instruction_t;

typedef enum {
  EEDI  =  1,   /* EEPROM Data In */
  EEDO  =  2,   /* EEPROM Data Out */
  EECLK =  4,   /* EEPROM Serial Clock */
  EESEL =  8,   /* EEPROM Chip Select */
  MDIO  = 16,   /* MII Management Data */
  MDDIR = 32,   /* MII Management Direction */
  MDC   = 64    /* MII Management Clock */
} eeprom_bits_t;

typedef struct {
  eeprom_bits_t state;
  uint16_t command;
  uint16_t data;
  uint8_t  count;
  uint8_t  address;
  uint16_t memory[16];
} eeprom_state_t;

static uint16_t eeprom_map[16] = {
    /* Only 12 words are used. */
    0xd008,
    0x0400,
    0x2cd0,
    0xcf82,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0xa098,
    0x0055
};

/* Code for saving and restoring of EEPROM state. */

static int eeprom_instance = 0;
static const int eeprom_version = 20060726;

static void eeprom_save(QEMUFile *f, void *opaque)
{
    eeprom_state_t *eeprom = (eeprom_state_t *)opaque;
    /* TODO: support different endianess */
    qemu_put_buffer(f, (uint8_t *)eeprom, sizeof(*eeprom));
}

int eeprom_load(QEMUFile *f, void *opaque, int version_id)
{
    eeprom_state_t *eeprom = (eeprom_state_t *)opaque;
    int result = 0;
    if (version_id == eeprom_version) {
        /* TODO: support different endianess */
        qemu_get_buffer(f, (uint8_t *)eeprom, sizeof(*eeprom));
    } else {
        result = -EINVAL;
    }
    return result;
}

/* */

static uint16_t eeprom_action(eeprom_state_t *ee, eeprom_bits_t bits)
{
  uint16_t command = ee->command;
  uint8_t address = ee->address;
  uint8_t *count = &ee->count;
  eeprom_bits_t state = ee->state;

  if (bits == -1) {
    if (command == eeprom_read) {
      if (*count > 25)
        logout("read data = 0x%04x, address = %u, bit = %d, state 0x%04x\n",
          ee->data, address, 26 - *count, state);
    }
    bits = state;
  } else if (bits & EESEL) {
    /* EEPROM is selected */
    if (!(state & EESEL)) {
      logout("selected, state 0x%04x => 0x%04x\n", state, bits);
    } else if (!(state & EECLK) && (bits & EECLK)) {
      /* Raising edge of clock. */
      //~ logout("raising clock, state 0x%04x => 0x%04x\n", state, bits);
      if (*count < 10) {
        ee->data = (ee->data << 1);
        if (bits & EEDI) {
          ee->data++;
        } else if (*count == 1) {
          *count = 0;
        }
        //~ logout("   count = %d, data = 0x%04x\n", *count, data);
        *count++;
        if (*count == 10) {
          ee->address = address = (ee->data & eeprom_amask);
          ee->command = command = (ee->data & eeprom_imask);
          ee->data = eeprom_map[address];
          logout("count = %d, command = 0x%02x, address = 0x%02x, data = 0x%04x\n",
            *count, command, address, ee->data);
        }
      //~ } else if (*count == 1 && !(bits & EEDI)) {
        /* Got start bit. */
      } else if (*count < 10 + 16) {
        if (command == eeprom_read) {
          bits = (bits & ~EEDO);
          if (ee->data & (1 << (25 - *count))) {
            bits += EEDO;
          }
        } else {
          logout("   command = 0x%04x, count = %d, data = 0x%04x\n",
            command, *count, ee->data);
        }
        *count++;
      } else {
        logout("??? state 0x%04x => 0x%04x\n", state, bits);
      }
    } else {
      //~ logout("state 0x%04x => 0x%04x\n", state, bits);
    }
  } else {
    logout("not selected, count = %u, state 0x%04x => 0x%04x\n", *count, state, bits);
    ee->data = 0;
    ee->count = 0;
    ee->address = 0;
    ee->command = 0;
  }
  ee->state = state = bits;
  return state;
}

#else

#include "eeprom9346.h"

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

typedef struct EEprom9346 {
    Chip9346Mode mode;
    uint32_t tick;
    uint8_t  address;
    uint16_t input;
    uint16_t output;

    uint8_t eecs;
    uint8_t eesk;
    uint8_t eedi;
    uint8_t eedo;

    uint8_t value;
    uint8_t size;
    uint16_t contents[0];
} EEProm9346;

static void eeprom_decode_command(eeprom_t *eeprom, uint8_t command)
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

static void prom9346_shift_clock(eeprom_t *eeprom)
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
                for (i = 0; i < eeprom->size; i++) {
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

static int prom9346_get_wire(eeprom_t *eeprom)
{
    if (!eeprom->eecs)
        return 0;

    return eeprom->eedo;
}

static void prom9346_set_wire(eeprom_t *eeprom, int eecs, int eesk, int eedi)
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

void eeprom9346_write(eeprom_t *eeprom, uint32_t val)
{
    val &= 0xff;

    logout("write val=0x%02x\n", val);

    /* mask unwriteable bits */
    //~ val = SET_MASKED(val, 0x31, eeprom->value);

    int eecs = ((val & EEPROM_CS) != 0);
    int eesk = ((val & EEPROM_SK) != 0);
    int eedi = ((val & EEPROM_DI) != 0);
    prom9346_set_wire(eeprom, eecs, eesk, eedi);

    eeprom->value = val;
}

uint32_t eeprom9346_read(eeprom_t *eeprom)
{
    uint32_t ret = eeprom->value;

    int eedo = prom9346_get_wire(eeprom);
    if (eedo) {
        ret |=  EEPROM_DO;
    } else {
        ret &= ~EEPROM_DO;
    }

    logout("read val=0x%02x\n", ret);

    return ret;
}

void eeprom9346_reset(eeprom_t *eeprom, const uint8_t *macaddr)
{
    /* prepare eeprom */
    size_t i;
    //~ !!! fixme
    memcpy(&eeprom->contents[0], macaddr, 6);
    eeprom->contents[0xa] = 0x4000;
    uint16_t sum = 0;
    for (i = 0; i < eeprom->size - 1; i++) {
        sum += eeprom->contents[i];
    }
    eeprom->contents[eeprom->size - 1] = 0xbaba - sum;
}

eeprom_t *eeprom9346_new(uint16_t nwords)
{
    eeprom_t *eeprom = (eeprom_t *)qemu_mallocz(sizeof(*eeprom) + nwords * 2);
    eeprom->size = nwords;
    return eeprom;
}

void eeprom9346_free(eeprom_t *eeprom)
{
    qemu_free(eeprom);
}

#endif
