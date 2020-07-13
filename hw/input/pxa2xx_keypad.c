/*
 * Intel PXA27X Keypad Controller emulation.
 *
 * Copyright (c) 2007 MontaVista Software, Inc
 * Written by Armin Kuster <akuster@kama-aina.net>
 *              or  <Akuster@mvista.com>
 *
 * This code is licensed under the GPLv2.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "hw/arm/pxa.h"
#include "ui/console.h"

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
    MemoryRegion iomem;
    qemu_irq    irq;
    const struct  keymap *map;
    int         pressed_cnt;
    int         alt_code;

    uint32_t    kpc;
    uint32_t    kpdk;
    uint32_t    kprec;
    uint32_t    kpmk;
    uint32_t    kpas;
    uint32_t    kpasmkp[4];
    uint32_t    kpkdi;
};

static void pxa27x_keypad_find_pressed_key(PXA2xxKeyPadState *kp, int *row, int *col)
{
    int i;
    for (i = 0; i < 4; i++)
    {
        *col = i * 2;
        for (*row = 0; *row < 8; (*row)++) {
            if (kp->kpasmkp[i] & (1 << *row))
                return;
        }
        *col = i * 2 + 1;
        for (*row = 0; *row < 8; (*row)++) {
            if (kp->kpasmkp[i] & (1 << (*row + 16)))
                return;
        }
    }
}

static void pxa27x_keyboard_event (PXA2xxKeyPadState *kp, int keycode)
{
    int row, col, rel, assert_irq = 0;
    uint32_t val;

    if (keycode == 0xe0) {
        kp->alt_code = 1;
        return;
    }

    if(!(kp->kpc & KPC_ME)) /* skip if not enabled */
        return;

    rel = (keycode & 0x80) ? 1 : 0; /* key release from qemu */
    keycode &= ~0x80; /* strip qemu key release bit */
    if (kp->alt_code) {
        keycode |= 0x80;
        kp->alt_code = 0;
    }

    row = kp->map[keycode].row;
    col = kp->map[keycode].column;
    if (row == -1 || col == -1) {
        return;
    }

    val = KPASMKPx_MKC(row, col);
    if (rel) {
        if (kp->kpasmkp[col / 2] & val) {
            kp->kpasmkp[col / 2] &= ~val;
            kp->pressed_cnt--;
            assert_irq = 1;
        }
    } else {
        if (!(kp->kpasmkp[col / 2] & val)) {
            kp->kpasmkp[col / 2] |= val;
            kp->pressed_cnt++;
            assert_irq = 1;
        }
    }
    kp->kpas = ((kp->pressed_cnt & 0x1f) << 26) | (0xf << 4) | 0xf;
    if (kp->pressed_cnt == 1) {
        kp->kpas &= ~((0xf << 4) | 0xf);
        if (rel) {
            pxa27x_keypad_find_pressed_key(kp, &row, &col);
        }
        kp->kpas |= ((row & 0xf) << 4) | (col & 0xf);
    }

    if (!(kp->kpc & (KPC_AS | KPC_ASACT)))
        assert_irq = 0;

    if (assert_irq && (kp->kpc & KPC_MIE)) {
        kp->kpc |= KPC_MI;
        qemu_irq_raise(kp->irq);
    }
}

static uint64_t pxa2xx_keypad_read(void *opaque, hwaddr offset,
                                   unsigned size)
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
    case KPDK:
        return s->kpdk;
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
    case KPMK:
        tmp = s->kpmk;
        if(tmp & KPMK_MKP)
            s->kpmk &= ~(KPMK_MKP);
        return tmp;
    case KPAS:
        return s->kpas;
    case KPASMKP0:
        return s->kpasmkp[0];
    case KPASMKP1:
        return s->kpasmkp[1];
    case KPASMKP2:
        return s->kpasmkp[2];
    case KPASMKP3:
        return s->kpasmkp[3];
    case KPKDI:
        return s->kpkdi;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad read offset 0x%"HWADDR_PRIx"\n",
                      __func__, offset);
    }

    return 0;
}

static void pxa2xx_keypad_write(void *opaque, hwaddr offset,
                                uint64_t value, unsigned size)
{
    PXA2xxKeyPadState *s = (PXA2xxKeyPadState *) opaque;

    switch (offset) {
    case KPC:
        s->kpc = value;
        if (s->kpc & KPC_AS) {
            s->kpc &= ~(KPC_AS);
        }
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
        s->kpasmkp[0] = value;
        break;
    case KPASMKP1:
        s->kpasmkp[1] = value;
        break;
    case KPASMKP2:
        s->kpasmkp[2] = value;
        break;
    case KPASMKP3:
        s->kpasmkp[3] = value;
        break;
    case KPKDI:
        s->kpkdi = value;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad write offset 0x%"HWADDR_PRIx"\n",
                      __func__, offset);
    }
}

static const MemoryRegionOps pxa2xx_keypad_ops = {
    .read = pxa2xx_keypad_read,
    .write = pxa2xx_keypad_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_pxa2xx_keypad = {
    .name = "pxa2xx_keypad",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(kpc, PXA2xxKeyPadState),
        VMSTATE_UINT32(kpdk, PXA2xxKeyPadState),
        VMSTATE_UINT32(kprec, PXA2xxKeyPadState),
        VMSTATE_UINT32(kpmk, PXA2xxKeyPadState),
        VMSTATE_UINT32(kpas, PXA2xxKeyPadState),
        VMSTATE_UINT32_ARRAY(kpasmkp, PXA2xxKeyPadState, 4),
        VMSTATE_UINT32(kpkdi, PXA2xxKeyPadState),
        VMSTATE_END_OF_LIST()
    }
};

PXA2xxKeyPadState *pxa27x_keypad_init(MemoryRegion *sysmem,
                                      hwaddr base,
                                      qemu_irq irq)
{
    PXA2xxKeyPadState *s;

    s = (PXA2xxKeyPadState *) g_malloc0(sizeof(PXA2xxKeyPadState));
    s->irq = irq;

    memory_region_init_io(&s->iomem, NULL, &pxa2xx_keypad_ops, s,
                          "pxa2xx-keypad", 0x00100000);
    memory_region_add_subregion(sysmem, base, &s->iomem);

    vmstate_register(NULL, 0, &vmstate_pxa2xx_keypad, s);

    return s;
}

void pxa27x_register_keypad(PXA2xxKeyPadState *kp,
                            const struct keymap *map, int size)
{
    if(!map || size < 0x80) {
        fprintf(stderr, "%s - No PXA keypad map defined\n", __func__);
        exit(-1);
    }

    kp->map = map;
    qemu_add_kbd_event_handler((QEMUPutKBDEvent *) pxa27x_keyboard_event, kp);
}
