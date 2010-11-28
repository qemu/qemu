/*
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */
#ifndef EVENT_H
#define EVENT_H 1
#include "eventt.h"
#include "vreadert.h"
#include "vcardt.h"

VEvent *vevent_new(VEventType type, VReader *reader, VCard *card);
void vevent_delete(VEvent *);

/*
 * VEvent queueing services
 */
void vevent_queue_vevent(VEvent *);
void vevent_queue_init(void);

/*
 *  VEvent dequeing services
 */
VEvent *vevent_wait_next_vevent(void);
VEvent *vevent_get_next_vevent(void);


#endif
