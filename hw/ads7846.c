/*
 * TI ADS7846 / TSC2046 chip emulation.
 *
 * Copyright (c) 2006 Openedhand Ltd.
 * Written by Andrzej Zaborowski <balrog@zabor.org>
 *
 * This code is licensed under the GNU GPL v2.
 */

#include "hw.h"
#include "devices.h"
#include "console.h"

struct ads7846_state_s {
    qemu_irq interrupt;

    int input[8];
    int pressure;
    int noise;

    int cycle;
    int output;
};

/* Control-byte bitfields */
#define CB_PD0		(1 << 0)
#define CB_PD1		(1 << 1)
#define CB_SER		(1 << 2)
#define CB_MODE		(1 << 3)
#define CB_A0		(1 << 4)
#define CB_A1		(1 << 5)
#define CB_A2		(1 << 6)
#define CB_START	(1 << 7)

#define X_AXIS_DMAX	3470
#define X_AXIS_MIN	290
#define Y_AXIS_DMAX	3450
#define Y_AXIS_MIN	200

#define ADS_VBAT	2000
#define ADS_VAUX	2000
#define ADS_TEMP0	2000
#define ADS_TEMP1	3000
#define ADS_XPOS(x, y)	(X_AXIS_MIN + ((X_AXIS_DMAX * (x)) >> 15))
#define ADS_YPOS(x, y)	(Y_AXIS_MIN + ((Y_AXIS_DMAX * (y)) >> 15))
#define ADS_Z1POS(x, y)	600
#define ADS_Z2POS(x, y)	(600 + 6000 / ADS_XPOS(x, y))

static void ads7846_int_update(struct ads7846_state_s *s)
{
    if (s->interrupt)
        qemu_set_irq(s->interrupt, s->pressure == 0);
}

uint32_t ads7846_read(void *opaque)
{
    struct ads7846_state_s *s = (struct ads7846_state_s *) opaque;

    return s->output;
}

void ads7846_write(void *opaque, uint32_t value)
{
    struct ads7846_state_s *s = (struct ads7846_state_s *) opaque;

    switch (s->cycle ++) {
    case 0:
        if (!(value & CB_START)) {
            s->cycle = 0;
            break;
        }

        s->output = s->input[(value >> 4) & 7];

        /* Imitate the ADC noise, some drivers expect this.  */
        s->noise = (s->noise + 3) & 7;
        switch ((value >> 4) & 7) {
        case 1: s->output += s->noise ^ 2; break;
        case 3: s->output += s->noise ^ 0; break;
        case 4: s->output += s->noise ^ 7; break;
        case 5: s->output += s->noise ^ 5; break;
        }

        if (value & CB_MODE)
            s->output >>= 4;	/* 8 bits instead of 12 */

        break;
    case 1:
        s->cycle = 0;
        break;
    }
}

static void ads7846_ts_event(void *opaque,
                int x, int y, int z, int buttons_state)
{
    struct ads7846_state_s *s = opaque;

    if (buttons_state) {
        x = 0x7fff - x;
        s->input[1] = ADS_XPOS(x, y);
        s->input[3] = ADS_Z1POS(x, y);
        s->input[4] = ADS_Z2POS(x, y);
        s->input[5] = ADS_YPOS(x, y);
    }

    if (s->pressure == !buttons_state) {
        s->pressure = !!buttons_state;

        ads7846_int_update(s);
    }
}

static void ads7846_save(QEMUFile *f, void *opaque)
{
    struct ads7846_state_s *s = (struct ads7846_state_s *) opaque;
    int i;

    for (i = 0; i < 8; i ++)
        qemu_put_be32(f, s->input[i]);
    qemu_put_be32(f, s->noise);
    qemu_put_be32(f, s->cycle);
    qemu_put_be32(f, s->output);
}

static int ads7846_load(QEMUFile *f, void *opaque, int version_id)
{
    struct ads7846_state_s *s = (struct ads7846_state_s *) opaque;
    int i;

    for (i = 0; i < 8; i ++)
        s->input[i] = qemu_get_be32(f);
    s->noise = qemu_get_be32(f);
    s->cycle = qemu_get_be32(f);
    s->output = qemu_get_be32(f);

    s->pressure = 0;
    ads7846_int_update(s);

    return 0;
}

struct ads7846_state_s *ads7846_init(qemu_irq penirq)
{
    struct ads7846_state_s *s;
    s = (struct ads7846_state_s *)
            qemu_mallocz(sizeof(struct ads7846_state_s));
    memset(s, 0, sizeof(struct ads7846_state_s));

    s->interrupt = penirq;

    s->input[0] = ADS_TEMP0;	/* TEMP0 */
    s->input[2] = ADS_VBAT;	/* VBAT */
    s->input[6] = ADS_VAUX;	/* VAUX */
    s->input[7] = ADS_TEMP1;	/* TEMP1 */

    /* We want absolute coordinates */
    qemu_add_mouse_event_handler(ads7846_ts_event, s, 1,
                    "QEMU ADS7846-driven Touchscreen");

    ads7846_int_update(s);

    register_savevm("ads7846", -1, 0, ads7846_save, ads7846_load, s);

    return s;
}
