/*
 * QEMU EEPROM 93xx emulation
 *
 * Copyright (c) 2006-2007 Stefan Weil
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
 * NMC93C06 256-Bit (16 x 16)
 * NMC93C46 1024-Bit (64 x 16)
 * NMC93C56 2028 Bit (128 x 16)
 * NMC93C66 4096 Bit (256 x 16)
 * Compatible devices include FM93C46 and others.
 *
 * Other drivers use these interface functions:
 * eeprom93xx_new   - add a new EEPROM (with 16, 64 or 256 words)
 * eeprom93xx_free  - destroy EEPROM
 * eeprom93xx_read  - read data from the EEPROM
 * eeprom93xx_write - write data to the EEPROM
 * eeprom93xx_data  - get EEPROM data array for external manipulation
 *
 * Todo list:
 * - No emulation of EEPROM timings.
 */

#include "hw.h"
#include "eeprom93xx.h"

/* Debug EEPROM emulation. */
//~ #define DEBUG_EEPROM

#ifdef DEBUG_EEPROM
#define logout(fmt, ...) fprintf(stderr, "EEPROM\t%-24s" fmt, __func__, ## __VA_ARGS__)
#else
#define logout(fmt, ...) ((void)0)
#endif

#define EEPROM_INSTANCE  0
#define OLD_EEPROM_VERSION 20061112
#define EEPROM_VERSION (OLD_EEPROM_VERSION + 1)

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
#endif

#ifdef DEBUG_EEPROM
static const char *opstring[] = {
  "extended", "write", "read", "erase"
};
#endif

struct _eeprom_t {
    uint8_t  tick;
    uint8_t  address;
    uint8_t  command;
    uint8_t  writeable;

    uint8_t eecs;
    uint8_t eesk;
    uint8_t eedo;

    uint8_t  addrbits;
    uint16_t size;
    uint16_t data;
    uint16_t contents[0];
};

/* Code for saving and restoring of EEPROM state. */

static void eeprom_save(QEMUFile *f, void *opaque)
{
    /* Save EEPROM data. */
    unsigned address;
    eeprom_t *eeprom = (eeprom_t *)opaque;

    qemu_put_byte(f, eeprom->tick);
    qemu_put_byte(f, eeprom->address);
    qemu_put_byte(f, eeprom->command);
    qemu_put_byte(f, eeprom->writeable);

    qemu_put_byte(f, eeprom->eecs);
    qemu_put_byte(f, eeprom->eesk);
    qemu_put_byte(f, eeprom->eedo);

    qemu_put_byte(f, eeprom->addrbits);
    qemu_put_be16(f, eeprom->size);
    qemu_put_be16(f, eeprom->data);
    for (address = 0; address < eeprom->size; address++) {
        qemu_put_be16(f, eeprom->contents[address]);
    }
}

static int eeprom_load(QEMUFile *f, void *opaque, int version_id)
{
    /* Load EEPROM data from saved data if version and EEPROM size
       of data and current EEPROM are identical. */
    eeprom_t *eeprom = (eeprom_t *)opaque;
    int result = -EINVAL;
    if (version_id >= OLD_EEPROM_VERSION) {
        unsigned address;
        int size = eeprom->size;

        eeprom->tick = qemu_get_byte(f);
        eeprom->address = qemu_get_byte(f);
        eeprom->command = qemu_get_byte(f);
        eeprom->writeable = qemu_get_byte(f);

        eeprom->eecs = qemu_get_byte(f);
        eeprom->eesk = qemu_get_byte(f);
        eeprom->eedo = qemu_get_byte(f);

        eeprom->addrbits = qemu_get_byte(f);
        if (version_id == OLD_EEPROM_VERSION) {
            eeprom->size = qemu_get_byte(f);
            qemu_get_byte(f);
        } else {
            eeprom->size = qemu_get_be16(f);
        }

        if (eeprom->size == size) {
            eeprom->data = qemu_get_be16(f);
            for (address = 0; address < eeprom->size; address++) {
                eeprom->contents[address] = qemu_get_be16(f);
            }
            result = 0;
        }
    }
    return result;
}

void eeprom93xx_write(eeprom_t *eeprom, int eecs, int eesk, int eedi)
{
    uint8_t tick = eeprom->tick;
    uint8_t eedo = eeprom->eedo;
    uint16_t address = eeprom->address;
    uint8_t command = eeprom->command;

    logout("CS=%u SK=%u DI=%u DO=%u, tick = %u\n",
           eecs, eesk, eedi, eedo, tick);

    if (! eeprom->eecs && eecs) {
        /* Start chip select cycle. */
        logout("Cycle start, waiting for 1st start bit (0)\n");
        tick = 0;
        command = 0x0;
        address = 0x0;
    } else if (eeprom->eecs && ! eecs) {
        /* End chip select cycle. This triggers write / erase. */
        if (eeprom->writeable) {
            uint8_t subcommand = address >> (eeprom->addrbits - 2);
            if (command == 0 && subcommand == 2) {
                /* Erase all. */
                for (address = 0; address < eeprom->size; address++) {
                    eeprom->contents[address] = 0xffff;
                }
            } else if (command == 3) {
                /* Erase word. */
                eeprom->contents[address] = 0xffff;
            } else if (tick >= 2 + 2 + eeprom->addrbits + 16) {
                if (command == 1) {
                    /* Write word. */
                    eeprom->contents[address] &= eeprom->data;
                } else if (command == 0 && subcommand == 1) {
                    /* Write all. */
                    for (address = 0; address < eeprom->size; address++) {
                        eeprom->contents[address] &= eeprom->data;
                    }
                }
            }
        }
        /* Output DO is tristate, read results in 1. */
        eedo = 1;
    } else if (eecs && ! eeprom->eesk && eesk) {
        /* Raising edge of clock shifts data in. */
        if (tick == 0) {
            /* Wait for 1st start bit. */
            if (eedi == 0) {
                logout("Got correct 1st start bit, waiting for 2nd start bit (1)\n");
                tick++;
            } else {
                logout("wrong 1st start bit (is 1, should be 0)\n");
                tick = 2;
                //~ assert(!"wrong start bit");
            }
        } else if (tick == 1) {
            /* Wait for 2nd start bit. */
            if (eedi != 0) {
                logout("Got correct 2nd start bit, getting command + address\n");
                tick++;
            } else {
                logout("1st start bit is longer than needed\n");
            }
        } else if (tick < 2 + 2) {
            /* Got 2 start bits, transfer 2 opcode bits. */
            tick++;
            command <<= 1;
            if (eedi) {
                command += 1;
            }
        } else if (tick < 2 + 2 + eeprom->addrbits) {
            /* Got 2 start bits and 2 opcode bits, transfer all address bits. */
            tick++;
            address = ((address << 1) | eedi);
            if (tick == 2 + 2 + eeprom->addrbits) {
                logout("%s command, address = 0x%02x (value 0x%04x)\n",
                       opstring[command], address, eeprom->contents[address]);
                if (command == 2) {
                    eedo = 0;
                }
                address = address % eeprom->size;
                if (command == 0) {
                    /* Command code in upper 2 bits of address. */
                    switch (address >> (eeprom->addrbits - 2)) {
                        case 0:
                            logout("write disable command\n");
                            eeprom->writeable = 0;
                            break;
                        case 1:
                            logout("write all command\n");
                            break;
                        case 2:
                            logout("erase all command\n");
                            break;
                        case 3:
                            logout("write enable command\n");
                            eeprom->writeable = 1;
                            break;
                    }
                } else {
                    /* Read, write or erase word. */
                    eeprom->data = eeprom->contents[address];
                }
            }
        } else if (tick < 2 + 2 + eeprom->addrbits + 16) {
            /* Transfer 16 data bits. */
            tick++;
            if (command == 2) {
                /* Read word. */
                eedo = ((eeprom->data & 0x8000) != 0);
            }
            eeprom->data <<= 1;
            eeprom->data += eedi;
        } else {
            logout("additional unneeded tick, not processed\n");
        }
    }
    /* Save status of EEPROM. */
    eeprom->tick = tick;
    eeprom->eecs = eecs;
    eeprom->eesk = eesk;
    eeprom->eedo = eedo;
    eeprom->address = address;
    eeprom->command = command;
}

uint16_t eeprom93xx_read(eeprom_t *eeprom)
{
    /* Return status of pin DO (0 or 1). */
    logout("CS=%u DO=%u\n", eeprom->eecs, eeprom->eedo);
    return (eeprom->eedo);
}

#if 0
void eeprom93xx_reset(eeprom_t *eeprom)
{
    /* prepare eeprom */
    logout("eeprom = 0x%p\n", eeprom);
    eeprom->tick = 0;
    eeprom->command = 0;
}
#endif

eeprom_t *eeprom93xx_new(uint16_t nwords)
{
    /* Add a new EEPROM (with 16, 64 or 256 words). */
    eeprom_t *eeprom;
    uint8_t addrbits;

    switch (nwords) {
        case 16:
        case 64:
            addrbits = 6;
            break;
        case 128:
        case 256:
            addrbits = 8;
            break;
        default:
            assert(!"Unsupported EEPROM size, fallback to 64 words!");
            nwords = 64;
            addrbits = 6;
    }

    eeprom = (eeprom_t *)qemu_mallocz(sizeof(*eeprom) + nwords * 2);
    eeprom->size = nwords;
    eeprom->addrbits = addrbits;
    /* Output DO is tristate, read results in 1. */
    eeprom->eedo = 1;
    logout("eeprom = 0x%p, nwords = %u\n", eeprom, nwords);
    register_savevm("eeprom", EEPROM_INSTANCE, EEPROM_VERSION,
                    eeprom_save, eeprom_load, eeprom);
    return eeprom;
}

void eeprom93xx_free(eeprom_t *eeprom)
{
    /* Destroy EEPROM. */
    logout("eeprom = 0x%p\n", eeprom);
    qemu_free(eeprom);
}

uint16_t *eeprom93xx_data(eeprom_t *eeprom)
{
    /* Get EEPROM data array. */
    return &eeprom->contents[0];
}

/* eof */
