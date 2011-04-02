/*
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#ifndef EVENTT_H
#define EVENTT_H 1
#include "vreadert.h"
#include "vcardt.h"

typedef struct VEventStruct VEvent;

typedef enum {
    VEVENT_READER_INSERT,
    VEVENT_READER_REMOVE,
    VEVENT_CARD_INSERT,
    VEVENT_CARD_REMOVE,
    VEVENT_LAST,
} VEventType;

struct VEventStruct {
    VEvent *next;
    VEventType type;
    VReader *reader;
    VCard *card;
};
#endif


