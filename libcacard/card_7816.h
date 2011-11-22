/*
 * Implement the 7816 portion of the card spec
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */
#ifndef CARD_7816_H
#define CARD_7816_H  1

#include "card_7816t.h"
#include "vcardt.h"

/*
 * constructors for VCardResponse's
 */
/* response from a return buffer and a status */
VCardResponse *vcard_response_new(VCard *card, unsigned char *buf, int len,
                                  int Le, vcard_7816_status_t status);
/* response from a return buffer and status bytes */
VCardResponse *vcard_response_new_bytes(VCard *card, unsigned char *buf,
                                        int len, int Le,
                                        unsigned char sw1, unsigned char sw2);
/* response from just status bytes */
VCardResponse *vcard_response_new_status_bytes(unsigned char sw1,
                                               unsigned char sw2);
/* response from just status: NOTE this cannot fail, it will always return a
 * valid response, if it can't allocate memory, the response will be
 * VCARD7816_STATUS_EXC_ERROR_MEMORY_FAILURE */
VCardResponse *vcard_make_response(vcard_7816_status_t status);

/* create a raw response (status has already been encoded */
VCardResponse *vcard_response_new_data(unsigned char *buf, int len);




/*
 * destructor for VCardResponse.
 *  Can be called with a NULL response
 */
void vcard_response_delete(VCardResponse *response);

/*
 * constructor for VCardAPDU
 */
VCardAPDU *vcard_apdu_new(unsigned char *raw_apdu, int len,
                          unsigned short *status);

/*
 * destructor for VCardAPDU
 *  Can be called with a NULL apdu
 */
void vcard_apdu_delete(VCardAPDU *apdu);

/*
 * APDU processing starts here. This routes the card processing stuff to the
 * right location. Always returns a valid response.
 */
VCardStatus vcard_process_apdu(VCard *card, VCardAPDU *apdu,
                               VCardResponse **response);

#endif
