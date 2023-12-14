/*
 * SD card bus interface code.
 *
 * Copyright (c) 2015 Linaro Limited
 *
 * Author:
 *  Peter Maydell <peter.maydell@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/qdev-core.h"
#include "hw/sd/sd.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "trace.h"

static inline const char *sdbus_name(SDBus *sdbus)
{
    return sdbus->qbus.name;
}

static SDState *get_card(SDBus *sdbus)
{
    /* We only ever have one child on the bus so just return it */
    BusChild *kid = QTAILQ_FIRST(&sdbus->qbus.children);

    if (!kid) {
        return NULL;
    }
    return SD_CARD(kid->child);
}

uint8_t sdbus_get_dat_lines(SDBus *sdbus)
{
    SDState *slave = get_card(sdbus);
    uint8_t dat_lines = 0b1111; /* 4 bit bus width */

    if (slave) {
        SDCardClass *sc = SD_CARD_GET_CLASS(slave);

        if (sc->get_dat_lines) {
            dat_lines = sc->get_dat_lines(slave);
        }
    }
    trace_sdbus_get_dat_lines(sdbus_name(sdbus), dat_lines);

    return dat_lines;
}

bool sdbus_get_cmd_line(SDBus *sdbus)
{
    SDState *slave = get_card(sdbus);
    bool cmd_line = true;

    if (slave) {
        SDCardClass *sc = SD_CARD_GET_CLASS(slave);

        if (sc->get_cmd_line) {
            cmd_line = sc->get_cmd_line(slave);
        }
    }
    trace_sdbus_get_cmd_line(sdbus_name(sdbus), cmd_line);

    return cmd_line;
}

void sdbus_set_voltage(SDBus *sdbus, uint16_t millivolts)
{
    SDState *card = get_card(sdbus);

    trace_sdbus_set_voltage(sdbus_name(sdbus), millivolts);
    if (card) {
        SDCardClass *sc = SD_CARD_GET_CLASS(card);

        assert(sc->set_voltage);
        sc->set_voltage(card, millivolts);
    }
}

int sdbus_do_command(SDBus *sdbus, SDRequest *req, uint8_t *response)
{
    SDState *card = get_card(sdbus);

    trace_sdbus_command(sdbus_name(sdbus), req->cmd, req->arg);
    if (card) {
        SDCardClass *sc = SD_CARD_GET_CLASS(card);

        return sc->do_command(card, req, response);
    }

    return 0;
}

void sdbus_write_byte(SDBus *sdbus, uint8_t value)
{
    SDState *card = get_card(sdbus);

    trace_sdbus_write(sdbus_name(sdbus), value);
    if (card) {
        SDCardClass *sc = SD_CARD_GET_CLASS(card);

        sc->write_byte(card, value);
    }
}

void sdbus_write_data(SDBus *sdbus, const void *buf, size_t length)
{
    SDState *card = get_card(sdbus);
    const uint8_t *data = buf;

    if (card) {
        SDCardClass *sc = SD_CARD_GET_CLASS(card);

        for (size_t i = 0; i < length; i++) {
            trace_sdbus_write(sdbus_name(sdbus), data[i]);
            sc->write_byte(card, data[i]);
        }
    }
}

uint8_t sdbus_read_byte(SDBus *sdbus)
{
    SDState *card = get_card(sdbus);
    uint8_t value = 0;

    if (card) {
        SDCardClass *sc = SD_CARD_GET_CLASS(card);

        value = sc->read_byte(card);
    }
    trace_sdbus_read(sdbus_name(sdbus), value);

    return value;
}

void sdbus_read_data(SDBus *sdbus, void *buf, size_t length)
{
    SDState *card = get_card(sdbus);
    uint8_t *data = buf;

    if (card) {
        SDCardClass *sc = SD_CARD_GET_CLASS(card);

        for (size_t i = 0; i < length; i++) {
            data[i] = sc->read_byte(card);
            trace_sdbus_read(sdbus_name(sdbus), data[i]);
        }
    }
}

bool sdbus_receive_ready(SDBus *sdbus)
{
    SDState *card = get_card(sdbus);

    if (card) {
        SDCardClass *sc = SD_CARD_GET_CLASS(card);

        return sc->receive_ready(card);
    }

    return false;
}

bool sdbus_data_ready(SDBus *sdbus)
{
    SDState *card = get_card(sdbus);

    if (card) {
        SDCardClass *sc = SD_CARD_GET_CLASS(card);

        return sc->data_ready(card);
    }

    return false;
}

bool sdbus_get_inserted(SDBus *sdbus)
{
    SDState *card = get_card(sdbus);

    if (card) {
        SDCardClass *sc = SD_CARD_GET_CLASS(card);

        return sc->get_inserted(card);
    }

    return false;
}

bool sdbus_get_readonly(SDBus *sdbus)
{
    SDState *card = get_card(sdbus);

    if (card) {
        SDCardClass *sc = SD_CARD_GET_CLASS(card);

        return sc->get_readonly(card);
    }

    return false;
}

void sdbus_set_inserted(SDBus *sdbus, bool inserted)
{
    SDBusClass *sbc = SD_BUS_GET_CLASS(sdbus);
    BusState *qbus = BUS(sdbus);

    if (sbc->set_inserted) {
        sbc->set_inserted(qbus->parent, inserted);
    }
}

void sdbus_set_readonly(SDBus *sdbus, bool readonly)
{
    SDBusClass *sbc = SD_BUS_GET_CLASS(sdbus);
    BusState *qbus = BUS(sdbus);

    if (sbc->set_readonly) {
        sbc->set_readonly(qbus->parent, readonly);
    }
}

void sdbus_reparent_card(SDBus *from, SDBus *to)
{
    SDState *card = get_card(from);
    SDCardClass *sc;
    bool readonly;

    /* We directly reparent the card object rather than implementing this
     * as a hotpluggable connection because we don't want to expose SD cards
     * to users as being hotpluggable, and we can get away with it in this
     * limited use case. This could perhaps be implemented more cleanly in
     * future by adding support to the hotplug infrastructure for "device
     * can be hotplugged only via code, not by user".
     */

    if (!card) {
        return;
    }

    sc = SD_CARD_GET_CLASS(card);
    readonly = sc->get_readonly(card);

    sdbus_set_inserted(from, false);
    qdev_set_parent_bus(DEVICE(card), &to->qbus, &error_abort);
    sdbus_set_inserted(to, true);
    sdbus_set_readonly(to, readonly);
}

static const TypeInfo sd_bus_types[] = {
    {
        .name           = TYPE_SD_BUS,
        .parent         = TYPE_BUS,
        .instance_size  = sizeof(SDBus),
        .class_size     = sizeof(SDBusClass),
    },
};

DEFINE_TYPES(sd_bus_types)
