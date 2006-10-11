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
 *
 * Todo list:
 * - "write all" command is unimplemented.
 * - No emulation of EEPROM timings.
 */

/* Debug EEPROM emulation. */
//~ #define DEBUG_EEPROM

#ifdef DEBUG_EEPROM
#define logout(fmt, args...) printf("EEPROM\t%-24s" fmt, __func__, ##args)
#else
#define logout(fmt, args...) ((void)0)
#endif

static int eeprom_instance = 0;
static const int eeprom_version = 20060726;

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

#include <assert.h>
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

typedef struct EEprom9346 {
    uint8_t  tick;
    uint8_t  address;
    uint8_t  command;
    uint8_t  readonly;
    uint16_t data;

    uint8_t eecs;
    uint8_t eesk;
    uint8_t eedi;
    uint8_t eedo;

    uint32_t value;
    uint8_t  addrbits;
    uint8_t  size;
    uint16_t contents[0];
} EEProm9346;

static void eeprom_save(QEMUFile *f, void *opaque)
{
    eeprom_t *eeprom = (eeprom_t *)opaque;
    /* TODO: support different endianess */
    qemu_put_buffer(f, (uint8_t *)eeprom, sizeof(*eeprom));
}

static int eeprom_load(QEMUFile *f, void *opaque, int version_id)
{
    eeprom_t *eeprom = (eeprom_t *)opaque;
    int result = 0;
    if (version_id == eeprom_version) {
        /* TODO: support different endianess */
        qemu_get_buffer(f, (uint8_t *)eeprom, sizeof(*eeprom));
    } else {
        result = -EINVAL;
    }
    return result;
}

void eeprom9346_write(eeprom_t *eeprom, int eecs, int eesk, int eedi)
{
    uint8_t tick = eeprom->tick;
    uint8_t eedo = eeprom->eedo;
    uint16_t address = eeprom->address;
    uint8_t command = eeprom->command;

    logout("CS=%u SK=%u DI=%u DO=%u, tick = %u, value = 0x%04x\n",
           eecs, eesk, eedi, eedo, tick, eeprom->value);

    if (! eeprom->eecs && eecs) {
        /* Start cycle. */
        logout("Cycle start, waiting for 1st start bit (0)\n");
        tick = 0;
        eeprom->value = 0x0;
        command = 0x0;
        address = 0x0;
        eedo = 1;
    } else if (eecs && ! eeprom->eesk && eesk) {
        /* Raising edge of clock shifts data in. */
        if (tick == 0) {
            /* Wait for 1st start bit. */
            if (eedi == 0) {
                logout("Got correct 1st start bit, waiting for 2nd start bit (1)\n");
                tick++;
            } else {
                assert(!"wrong start bit");
            }
        } else if (tick == 1) {
            /* Wait for 2nd start bit. */
            if (eedi == 1) {
                logout("Got correct 2nd start bit, getting command + address\n");
                tick++;
            } else {
                logout("1st start bit is longer than needed\n");
            }
        } else if (tick < 2 + 2) {
            /* Got 2 start bits, transfer 2 opcode bits. */
            tick++;
            command <<= 1;
            command += eedi;
            if (tick == 4) {
                switch (command) {
                    case 0:
                        /* Command code in upper 2 bits of address. */
                        break;
                    case 1:
                        logout("write command\n");
                        break;
                    case 2:
                        logout("read command\n");
                        break;
                      break;
                    case 3:
                        logout("erase command\n");
                        break;
                }
            }
        } else if (tick < 2 + 2 + eeprom->addrbits) {
            /* Got 2 start bits and 2 opcode bits, transfer all address bits. */
            tick++;
            address = ((address << 1) | eedi);
            if (tick == 2 + 2 + eeprom->addrbits) {
                logout("got address = %u\n", address);
                eedo = 0;
                address = address % eeprom->size;
                if (command == 0) {
                    /* Command code in upper 2 bits of address. */
                    switch (address >> (eeprom->addrbits - 2)) {
                        case 0:
                            logout("write disable command\n");
                            eeprom->readonly = 1;
                            break;
                        case 1:
                            logout("write all command\n");
                            assert(!"unimplemented write all command");
                            break;
                        case 2:
                            logout("erase all command\n");
                            memset(&eeprom->contents[0], 0, 2 * eeprom->size);
                            break;
                        case 3:
                            logout("write enable command\n");
                            eeprom->readonly = 0;
                            break;
                    }
                } else if (command == 3) {
                    /* Erase word. */
                    eeprom->contents[address] = 0;
                } else {
                    /* Read or write command. */
                    eeprom->data = eeprom->contents[address];
                }
            }
        } else if (tick < 2 + 2 + eeprom->addrbits + 16) {
            /* Transfer 16 data bits. */
            tick++;
            switch (command) {
                case 1:
                    eeprom->data <<= 1;
                    eeprom->data += eedi;
                    if (tick == 2 + 2 + eeprom->addrbits + 16) {
                        if (!eeprom->readonly) {
                            eeprom->contents[address] = eeprom->data;
                        }
                    }
                    break;
                case 2:
                    eedo = ((eeprom->data & 0x8000) != 0);
                    eeprom->data <<= 1;
                    break;
            }
        } else {
            logout("additional unneeded tick, not processed\n");
        }
    }
    eeprom->tick = tick;
    eeprom->eecs = eecs;
    eeprom->eesk = eesk;
    eeprom->eedo = eedo;
    eeprom->address = address;
    eeprom->command = command;
}

uint16_t eeprom9346_read(eeprom_t *eeprom)
{
    logout("CS=%u DO=%u\n", eeprom->eecs, eeprom->eedo);
    return (eeprom->eecs && eeprom->eedo);
}

void eeprom9346_reset(eeprom_t *eeprom, const uint8_t *macaddr)
{
    /* prepare eeprom */
    size_t i;
    logout("eeprom = 0x%p\n", eeprom);
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
    eeprom_t *eeprom;
    uint8_t addrbits;

    switch (nwords) {
        case 16:
            addrbits = 4;
            break;
        case 64:
            addrbits = 6;
            break;
        case 256:
            addrbits = 8;
            break;
        default:
            assert(!"Unsupported EEPROM size!");
            nwords = 64;
            addrbits = 6;
    }

    eeprom = (eeprom_t *)qemu_mallocz(sizeof(*eeprom) + nwords * 2);
    eeprom->size = nwords;
    eeprom->addrbits = addrbits;
    logout("eeprom = 0x%p, nwords = %u\n", eeprom, nwords);
    register_savevm("eeprom", eeprom_instance, eeprom_version,
                    eeprom_save, eeprom_load, eeprom);
    return eeprom;
}

void eeprom9346_free(eeprom_t *eeprom)
{
    logout("eeprom = 0x%p\n", eeprom);
    qemu_free(eeprom);
}

#endif
