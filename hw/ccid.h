/*
 * CCID Passthru Card Device emulation
 *
 * Copyright (c) 2011 Red Hat.
 * Written by Alon Levy.
 *
 * This code is licenced under the GNU LGPL, version 2 or later.
 */

#ifndef CCID_H
#define CCID_H

#include "qdev.h"

typedef struct CCIDCardState CCIDCardState;
typedef struct CCIDCardInfo CCIDCardInfo;

/*
 * state of the CCID Card device (i.e. hw/ccid-card-*.c)
 */
struct CCIDCardState {
    DeviceState qdev;
    uint32_t    slot; /* For future use with multiple slot reader. */
};

/*
 * callbacks to be used by the CCID device (hw/usb-ccid.c) to call
 * into the smartcard device (hw/ccid-card-*.c)
 */
struct CCIDCardInfo {
    DeviceInfo qdev;
    const uint8_t *(*get_atr)(CCIDCardState *card, uint32_t *len);
    void (*apdu_from_guest)(CCIDCardState *card,
                            const uint8_t *apdu,
                            uint32_t len);
    int (*exitfn)(CCIDCardState *card);
    int (*initfn)(CCIDCardState *card);
};

/*
 * API for smartcard calling the CCID device (used by hw/ccid-card-*.c)
 */
void ccid_card_send_apdu_to_guest(CCIDCardState *card,
                                  uint8_t *apdu,
                                  uint32_t len);
void ccid_card_card_removed(CCIDCardState *card);
void ccid_card_card_inserted(CCIDCardState *card);
void ccid_card_card_error(CCIDCardState *card, uint64_t error);
void ccid_card_qdev_register(CCIDCardInfo *card);

/*
 * support guest visible insertion/removal of ccid devices based on actual
 * devices connected/removed. Called by card implementation (passthru, local)
 */
int ccid_card_ccid_attach(CCIDCardState *card);
void ccid_card_ccid_detach(CCIDCardState *card);

#endif /* CCID_H */
