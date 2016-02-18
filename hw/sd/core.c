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
#include "sysemu/block-backend.h"
#include "hw/sd/sd.h"

static SDState *get_card(SDBus *sdbus)
{
    /* We only ever have one child on the bus so just return it */
    BusChild *kid = QTAILQ_FIRST(&sdbus->qbus.children);

    if (!kid) {
        return NULL;
    }
    return SD_CARD(kid->child);
}

int sdbus_do_command(SDBus *sdbus, SDRequest *req, uint8_t *response)
{
    SDState *card = get_card(sdbus);

    if (card) {
        SDCardClass *sc = SD_CARD_GET_CLASS(card);

        return sc->do_command(card, req, response);
    }

    return 0;
}

void sdbus_write_data(SDBus *sdbus, uint8_t value)
{
    SDState *card = get_card(sdbus);

    if (card) {
        SDCardClass *sc = SD_CARD_GET_CLASS(card);

        sc->write_data(card, value);
    }
}

uint8_t sdbus_read_data(SDBus *sdbus)
{
    SDState *card = get_card(sdbus);

    if (card) {
        SDCardClass *sc = SD_CARD_GET_CLASS(card);

        return sc->read_data(card);
    }

    return 0;
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

static const TypeInfo sd_bus_info = {
    .name = TYPE_SD_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(SDBus),
    .class_size = sizeof(SDBusClass),
};

static void sd_bus_register_types(void)
{
    type_register_static(&sd_bus_info);
}

type_init(sd_bus_register_types)
