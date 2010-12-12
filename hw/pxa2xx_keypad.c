/*
 * Intel PXA27X Keypad Controller emulation.
 *
 * Copyright (c) 2007 MontaVista Software, Inc
 * Written by Armin Kuster <akuster@kama-aina.net>
 *              or  <Akuster@mvista.com>
 *
 * This code is licensed under the GPLv2.
 */

#include "hw.h"
#include "pxa.h"
#include "console.h"

/*
 * Keypad
 */
#define KPC         0x00    /* Keypad Interface Control register */
#define KPDK        0x08    /* Keypad Interface Direct Key register */
#define KPREC       0x10    /* Keypad Interface Rotary Encoder register */
#define KPMK        0x18    /* Keypad Interface Matrix Key register */
#define KPAS        0x20    /* Keypad Interface Automatic Scan register */
#define KPASMKP0    0x28    /* Keypad Interface Automatic Scan Multiple
                                Key Presser register 0 */
#define KPASMKP1    0x30    /* Keypad Interface Automatic Scan Multiple
                                Key Presser register 1 */
#define KPASMKP2    0x38    /* Keypad Interface Automatic Scan Multiple
                                Key Presser register 2 */
#define KPASMKP3    0x40    /* Keypad Interface Automatic Scan Multiple
                                Key Presser register 3 */
#define KPKDI       0x48    /* Keypad Interface Key Debounce Interval
                                register */

/* Keypad defines */
#define KPC_AS          (0x1 << 30)  /* Automatic Scan bit */
#define KPC_ASACT       (0x1 << 29)  /* Automatic Scan on Activity */
#define KPC_MI          (0x1 << 22)  /* Matrix interrupt bit */
#define KPC_IMKP        (0x1 << 21)  /* Ignore Multiple Key Press */
#define KPC_MS7         (0x1 << 20)  /* Matrix scan line 7 */
#define KPC_MS6         (0x1 << 19)  /* Matrix scan line 6 */
#define KPC_MS5         (0x1 << 18)  /* Matrix scan line 5 */
#define KPC_MS4         (0x1 << 17)  /* Matrix scan line 4 */
#define KPC_MS3         (0x1 << 16)  /* Matrix scan line 3 */
#define KPC_MS2         (0x1 << 15)  /* Matrix scan line 2 */
#define KPC_MS1         (0x1 << 14)  /* Matrix scan line 1 */
#define KPC_MS0         (0x1 << 13)  /* Matrix scan line 0 */
#define KPC_ME          (0x1 << 12)  /* Matrix Keypad Enable */
#define KPC_MIE         (0x1 << 11)  /* Matrix Interrupt Enable */
#define KPC_DK_DEB_SEL  (0x1 <<  9)  /* Direct Keypad Debounce Select */
#define KPC_DI          (0x1 <<  5)  /* Direct key interrupt bit */
#define KPC_RE_ZERO_DEB (0x1 <<  4)  /* Rotary Encoder Zero Debounce */
#define KPC_REE1        (0x1 <<  3)  /* Rotary Encoder1 Enable */
#define KPC_REE0        (0x1 <<  2)  /* Rotary Encoder0 Enable */
#define KPC_DE          (0x1 <<  1)  /* Direct Keypad Enable */
#define KPC_DIE         (0x1 <<  0)  /* Direct Keypad interrupt Enable */

#define KPDK_DKP        (0x1 << 31)
#define KPDK_DK7        (0x1 <<  7)
#define KPDK_DK6        (0x1 <<  6)
#define KPDK_DK5        (0x1 <<  5)
#define KPDK_DK4        (0x1 <<  4)
#define KPDK_DK3        (0x1 <<  3)
#define KPDK_DK2        (0x1 <<  2)
#define KPDK_DK1        (0x1 <<  1)
#define KPDK_DK0        (0x1 <<  0)

#define KPREC_OF1       (0x1 << 31)
#define KPREC_UF1       (0x1 << 30)
#define KPREC_OF0       (0x1 << 15)
#define KPREC_UF0       (0x1 << 14)

#define KPMK_MKP        (0x1 << 31)
#define KPAS_SO         (0x1 << 31)
#define KPASMKPx_SO     (0x1 << 31)


#define KPASMKPx_MKC(row, col)  (1 << (row + 16 * (col % 2)))

#define PXAKBD_MAXROW   8
#define PXAKBD_MAXCOL   8

struct PXA2xxKeyPadState {
    qemu_irq    irq;
    struct  keymap *map;

    uint32_t    kpc;
    uint32_t    kpdk;
    uint32_t    kprec;
    uint32_t    kpmk;
    uint32_t    kpas;
    uint32_t    kpasmkp0;
    uint32_t    kpasmkp1;
    uint32_t    kpasmkp2;
    uint32_t    kpasmkp3;
    uint32_t    kpkdi;
};

static void pxa27x_keyboard_event (PXA2xxKeyPadState *kp, int keycode)
{
    int row, col,rel;

    if(!(kp->kpc & KPC_ME)) /* skip if not enabled */
        return;

    if(kp->kpc & KPC_AS || kp->kpc & KPC_ASACT) {
        if(kp->kpc & KPC_AS)
            kp->kpc &= ~(KPC_AS);

        rel = (keycode & 0x80) ? 1 : 0; /* key release from qemu */
        keycode &= ~(0x80); /* strip qemu key release bit */
        row = kp->map[keycode].row;
        col = kp->map[keycode].column;
        if(row == -1 || col == -1)
            return;
        switch (col) {
        case 0:
        case 1:
            if(rel)
                kp->kpasmkp0 = ~(0xffffffff);
            else
                kp->kpasmkp0 |= KPASMKPx_MKC(row,col);
            break;
        case 2:
        case 3:
            if(rel)
                kp->kpasmkp1 = ~(0xffffffff);
            else
                kp->kpasmkp1 |= KPASMKPx_MKC(row,col);
            break;
        case 4:
        case 5:
            if(rel)
                kp->kpasmkp2 = ~(0xffffffff);
            else
                kp->kpasmkp2 |= KPASMKPx_MKC(row,col);
            break;
        case 6:
        case 7:
            if(rel)
                kp->kpasmkp3 = ~(0xffffffff);
            else
                kp->kpasmkp3 |= KPASMKPx_MKC(row,col);
            break;
        } /* switch */
        goto out;
    }
    return;

out:
    if(kp->kpc & KPC_MIE) {
        kp->kpc |= KPC_MI;
        qemu_irq_raise(kp->irq);
    }
    return;
}

static uint32_t pxa2xx_keypad_read(void *opaque, target_phys_addr_t offset)
{
    PXA2xxKeyPadState *s = (PXA2xxKeyPadState *) opaque;
    uint32_t tmp;

    switch (offset) {
    case KPC:
        tmp = s->kpc;
        if(tmp & KPC_MI)
            s->kpc &= ~(KPC_MI);
        if(tmp & KPC_DI)
            s->kpc &= ~(KPC_DI);
        qemu_irq_lower(s->irq);
        return tmp;
        break;
    case KPDK:
        return s->kpdk;
        break;
    case KPREC:
        tmp = s->kprec;
        if(tmp & KPREC_OF1)
            s->kprec &= ~(KPREC_OF1);
        if(tmp & KPREC_UF1)
            s->kprec &= ~(KPREC_UF1);
        if(tmp & KPREC_OF0)
            s->kprec &= ~(KPREC_OF0);
        if(tmp & KPREC_UF0)
            s->kprec &= ~(KPREC_UF0);
        return tmp;
        break;
    case KPMK:
        tmp = s->kpmk;
        if(tmp & KPMK_MKP)
            s->kpmk &= ~(KPMK_MKP);
        return tmp;
        break;
    case KPAS:
        return s->kpas;
        break;
    case KPASMKP0:
        return s->kpasmkp0;
        break;
    case KPASMKP1:
        return s->kpasmkp1;
        break;
    case KPASMKP2:
        return s->kpasmkp2;
        break;
    case KPASMKP3:
        return s->kpasmkp3;
        break;
    case KPKDI:
        return s->kpkdi;
        break;
    default:
        hw_error("%s: Bad offset " REG_FMT "\n", __FUNCTION__, offset);
    }

    return 0;
}

static void pxa2xx_keypad_write(void *opaque,
                target_phys_addr_t offset, uint32_t value)
{
    PXA2xxKeyPadState *s = (PXA2xxKeyPadState *) opaque;

    switch (offset) {
    case KPC:
        s->kpc = value;
        break;
    case KPDK:
        s->kpdk = value;
        break;
    case KPREC:
        s->kprec = value;
        break;
    case KPMK:
        s->kpmk = value;
        break;
    case KPAS:
        s->kpas = value;
        break;
    case KPASMKP0:
        s->kpasmkp0 = value;
        break;
    case KPASMKP1:
        s->kpasmkp1 = value;
        break;
    case KPASMKP2:
        s->kpasmkp2 = value;
        break;
    case KPASMKP3:
        s->kpasmkp3 = value;
        break;
    case KPKDI:
        s->kpkdi = value;
        break;

    default:
        hw_error("%s: Bad offset " REG_FMT "\n", __FUNCTION__, offset);
    }
}

static CPUReadMemoryFunc * const pxa2xx_keypad_readfn[] = {
    pxa2xx_keypad_read,
    pxa2xx_keypad_read,
    pxa2xx_keypad_read
};

static CPUWriteMemoryFunc * const pxa2xx_keypad_writefn[] = {
    pxa2xx_keypad_write,
    pxa2xx_keypad_write,
    pxa2xx_keypad_write
};

static void pxa2xx_keypad_save(QEMUFile *f, void *opaque)
{
    PXA2xxKeyPadState *s = (PXA2xxKeyPadState *) opaque;

    qemu_put_be32s(f, &s->kpc);
    qemu_put_be32s(f, &s->kpdk);
    qemu_put_be32s(f, &s->kprec);
    qemu_put_be32s(f, &s->kpmk);
    qemu_put_be32s(f, &s->kpas);
    qemu_put_be32s(f, &s->kpasmkp0);
    qemu_put_be32s(f, &s->kpasmkp1);
    qemu_put_be32s(f, &s->kpasmkp2);
    qemu_put_be32s(f, &s->kpasmkp3);
    qemu_put_be32s(f, &s->kpkdi);

}

static int pxa2xx_keypad_load(QEMUFile *f, void *opaque, int version_id)
{
    PXA2xxKeyPadState *s = (PXA2xxKeyPadState *) opaque;

    qemu_get_be32s(f, &s->kpc);
    qemu_get_be32s(f, &s->kpdk);
    qemu_get_be32s(f, &s->kprec);
    qemu_get_be32s(f, &s->kpmk);
    qemu_get_be32s(f, &s->kpas);
    qemu_get_be32s(f, &s->kpasmkp0);
    qemu_get_be32s(f, &s->kpasmkp1);
    qemu_get_be32s(f, &s->kpasmkp2);
    qemu_get_be32s(f, &s->kpasmkp3);
    qemu_get_be32s(f, &s->kpkdi);

    return 0;
}

PXA2xxKeyPadState *pxa27x_keypad_init(target_phys_addr_t base,
        qemu_irq irq)
{
    int iomemtype;
    PXA2xxKeyPadState *s;

    s = (PXA2xxKeyPadState *) qemu_mallocz(sizeof(PXA2xxKeyPadState));
    s->irq = irq;

    iomemtype = cpu_register_io_memory(pxa2xx_keypad_readfn,
                    pxa2xx_keypad_writefn, s, DEVICE_NATIVE_ENDIAN);
    cpu_register_physical_memory(base, 0x00100000, iomemtype);

    register_savevm(NULL, "pxa2xx_keypad", 0, 0,
                    pxa2xx_keypad_save, pxa2xx_keypad_load, s);

    return s;
}

void pxa27x_register_keypad(PXA2xxKeyPadState *kp, struct keymap *map,
        int size)
{
    if(!map || size < 0x80) {
        fprintf(stderr, "%s - No PXA keypad map defined\n", __FUNCTION__);
        exit(-1);
    }

    kp->map = map;
    qemu_add_kbd_event_handler((QEMUPutKBDEvent *) pxa27x_keyboard_event, kp);
}
