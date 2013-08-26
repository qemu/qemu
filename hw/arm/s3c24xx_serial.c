/* hw/s3c24xx_serial.c
 *
 * Samsung S3C24XX Serial block
 *
 * Copyright 2006, 2007 Daniel Silverstone and Vincent Sanders
 *
 * Copyright 2010, 2013 Stefan Weil
 *
 * This file is under the terms of the GNU General Public License Version 2.
 */

#include "hw/hw.h"
#include "exec/address-spaces.h" /* get_system_memory */
#include "sysemu/char.h"
#include "sysemu/sysemu.h"

#include "s3c24xx.h"

#if 0
# define DEBUG_S3C24XX
#endif

#if defined(DEBUG_S3C24XX)
# define logout(fmt, ...) \
    fprintf(stderr, "S3C24xx\t%-24s" fmt, __func__, ##__VA_ARGS__)
#else
# define logout(fmt, ...) (void)0
#endif

/* S3C24XX serial port registers */

/* Line control         RW WORD */
#define S3C_SERIAL_ULCON 0x00
/* General control      RW WORD */
#define S3C_SERIAL_UCON  0x04
/* Fifo control         RW WORD */
#define S3C_SERIAL_UFCON 0x08
/* Modem control        RW WORD */
#define S3C_SERIAL_UMCON 0x0C
/* TX/RX Status         RO WORD */
#define S3C_SERIAL_UTRSTAT 0x10
/* Receive Error Status RO WORD */
#define S3C_SERIAL_UERSTAT 0x14
/* FiFo Status          RO WORD */
#define S3C_SERIAL_UFSTAT 0x18
/* Modem Status         RO WORD */
#define S3C_SERIAL_UMSTAT 0x1C
/* TX buffer            WR BYTE */
#define S3C_SERIAL_UTXH 0x20
/* RX buffer            RO BYTE */
#define S3C_SERIAL_URXH 0x24
/* BAUD Divisor         RW WORD */
#define S3C_SERIAL_UBRDIV 0x28

/* S3C24XX serial port state */
typedef struct s3c24xx_serial_dev_s {
    MemoryRegion mmio;
    uint32_t ulcon, ucon, ufcon, umcon, ubrdiv;
    unsigned char rx_byte;
    /* Byte is available to be read */
    unsigned int rx_available : 1;
    CharDriverState *chr;
    qemu_irq tx_irq;
    qemu_irq rx_irq;
    qemu_irq tx_level;
    qemu_irq rx_level;
} s3c24xx_serial_dev;

static void s3c24xx_serial_write(void *opaque, hwaddr addr,
                                 uint64_t value, unsigned size)
{
    s3c24xx_serial_dev *s = opaque;
    int reg = addr & 0x3f;

    logout("0x" TARGET_FMT_plx " 0x%08x\n", addr, value);

    switch(reg) {
    case S3C_SERIAL_ULCON:
        s->ulcon = value;
        break;

    case S3C_SERIAL_UCON:
        s->ucon = value;
        if( s->ucon & 1<<9 ) {
            qemu_set_irq(s->tx_level, 1);
        } else {
            qemu_set_irq(s->tx_level, 0);
        }
        if( !(s->ucon & 1<<8) ) {
            qemu_set_irq(s->rx_level, 0);
        }
        break;

    case S3C_SERIAL_UFCON:
        s->ufcon = (value & ~6);
        break;

    case S3C_SERIAL_UMCON:
        s->umcon = value;
        break;

    case S3C_SERIAL_UTRSTAT:
        break;

    case S3C_SERIAL_UERSTAT:
        break;

    case S3C_SERIAL_UFSTAT:
        break;

    case S3C_SERIAL_UMSTAT:
        break;

    case S3C_SERIAL_UTXH: {
        unsigned char ch = value & 0xff;
        if (s->chr && ((s->ucon & 1<<5)==0)) {
            qemu_chr_fe_write(s->chr, &ch, 1);
        } else {
            s->rx_byte = ch;
            s->rx_available = 1;
            if( s->ucon & 1<<8 ) {
                qemu_set_irq(s->rx_level, 1);
            } else {
                qemu_set_irq(s->rx_irq, 1);
            }
        }
        if (s->ucon & 1<<9) {
            qemu_set_irq(s->tx_level, 1);
        } else {
            qemu_set_irq(s->tx_irq, 1);
        }
        break;
    }

    case S3C_SERIAL_URXH:
        break;

    case S3C_SERIAL_UBRDIV:
        s->ubrdiv = value;
        break;

    default:
        break;
    };
}

static uint64_t s3c24xx_serial_read(void *opaque, hwaddr addr,
                                    unsigned size)
{
    s3c24xx_serial_dev *s = opaque;
    int reg = addr & 0x3f;

    logout("0x" TARGET_FMT_plx "\n", addr);

    switch (reg) {
    case S3C_SERIAL_ULCON:
        return s->ulcon;

    case S3C_SERIAL_UCON:
        return s->ucon;

    case S3C_SERIAL_UFCON:
        return s->ufcon & ~0x8; /* bit 3 is reserved, must be zero */

    case S3C_SERIAL_UMCON:
        return s->umcon & 0x11; /* Rest are reserved, must be zero */

    case S3C_SERIAL_UTRSTAT:
        return 6 | s->rx_available; /* TX always clear, RX when available */

    case S3C_SERIAL_UERSTAT:
        return 0; /* Later, break detect comes in here */

    case S3C_SERIAL_UFSTAT:
        return s->rx_available; /* TXFIFO, always empty, RXFIFO 0 or 1 bytes */

    case S3C_SERIAL_UMSTAT:
        return 0;

    case S3C_SERIAL_UTXH:
        return 0;

    case S3C_SERIAL_URXH:
        s->rx_available = 0;
        if (s->ucon & 1<<8) {
            qemu_set_irq(s->rx_level, 0);
        }
        return s->rx_byte;

    case S3C_SERIAL_UBRDIV:
        return s->ubrdiv;

    default:
        return 0;
    };
}

static const MemoryRegionOps s3c24xx_serial_ops = {
    .read = s3c24xx_serial_read,
    .write = s3c24xx_serial_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4
    }
};

static void s3c24xx_serial_event(void *opaque, int event)
{
}

static int
s3c24xx_serial_can_receive(void *opaque)
{
    s3c24xx_serial_dev *s = opaque;

    /* If there's no byte to be read, we can receive a new one */
    return !s->rx_available;
}

static void
s3c24xx_serial_receive(void *opaque, const uint8_t *buf, int size)
{
    s3c24xx_serial_dev *s = opaque;
    s->rx_byte = buf[0];
    s->rx_available = 1;
    if ( s->ucon & 1 << 8 ) {
        qemu_set_irq(s->rx_level, 1);
    } else {
        /* Is there something we can do here to ensure it's just a pulse ? */
        qemu_set_irq(s->rx_irq, 1);
    }
}

/* Create a S3C serial port, the port implementation is common to all
 * current s3c devices only differing in the I/O base address and number of
 * ports.
 */
struct s3c24xx_serial_dev_s *
s3c24xx_serial_init(S3CState *soc,
                    CharDriverState *chr,
                    hwaddr base_addr,
                    int irqn)
{
    /* Initialise a serial port at the given port address */
    s3c24xx_serial_dev *s = g_new0(s3c24xx_serial_dev, 1);

    /* initialise serial port context */
    s->rx_irq = s3c24xx_get_irq(soc->irq, irqn);
    s->rx_level = s3c24xx_get_irq(soc->irq, irqn + 64);

    s->tx_irq = s3c24xx_get_irq(soc->irq, irqn + 1);
    s->tx_level = s3c24xx_get_irq(soc->irq, irqn + 1 + 64);

    /* Register the MMIO region. */
    memory_region_init_io(&s->mmio, OBJECT(s), &s3c24xx_serial_ops, s,
                          "s3c24xx.serial", 44);
    memory_region_add_subregion(get_system_memory(), base_addr, &s->mmio);

    if (chr) {
        /* If the port is present add to the character device's IO handlers. */
        s->chr = chr;

        qemu_chr_add_handlers(s->chr,
                              s3c24xx_serial_can_receive,
                              s3c24xx_serial_receive,
                              s3c24xx_serial_event,
                              s);
    }
    return s;
}
