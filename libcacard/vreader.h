/*
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#ifndef VREADER_H
#define VREADER_H 1

#include "eventt.h"
#include "vreadert.h"
#include "vcardt.h"

/*
 * calls for reader front end
 */
VReaderStatus vreader_power_on(VReader *reader, unsigned char *atr, int *len);
VReaderStatus vreader_power_off(VReader *reader);
VReaderStatus vreader_xfr_bytes(VReader *reader, unsigned char *send_buf,
                                int send_buf_len, unsigned char *receive_buf,
                                int *receive_buf_len);

/* constructor */
VReader *vreader_new(const char *readerName, VReaderEmul *emul_private,
                     VReaderEmulFree private_free);
/* get a new reference to a reader */
VReader *vreader_reference(VReader *reader);
/* "destructor" (readers are reference counted) */
void vreader_free(VReader *reader);

/* accessors */
VReaderEmul *vreader_get_private(VReader *);
VReaderStatus vreader_card_is_present(VReader *reader);
void vreader_queue_card_event(VReader *reader);
const char *vreader_get_name(VReader *reader);
vreader_id_t vreader_get_id(VReader *reader);
VReaderStatus vreader_set_id(VReader *reader, vreader_id_t id);

/* list operations */
VReaderList *vreader_get_reader_list(void);
void vreader_list_delete(VReaderList *list);
VReader *vreader_list_get_reader(VReaderListEntry *entry);
VReaderListEntry *vreader_list_get_first(VReaderList *list);
VReaderListEntry *vreader_list_get_next(VReaderListEntry *list);
VReader *vreader_get_reader_by_id(vreader_id_t id);
VReader *vreader_get_reader_by_name(const char *name);

/*
 * list tools for vcard_emul
 */
void vreader_init(void);
VReaderStatus vreader_add_reader(VReader *reader);
VReaderStatus vreader_remove_reader(VReader *reader);
VReaderStatus vreader_insert_card(VReader *reader, VCard *card);

#endif
