/*
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */
#ifndef VCARD_H
#define VCARD_H 1

#include "vcardt.h"

/*
 * response buffer constructors and destructors.
 *
 * response buffers are used when we need to return more data than will fit in
 * a normal APDU response (nominally 254 bytes).
 */
VCardBufferResponse *vcard_buffer_response_new(unsigned char *buffer, int size);
void vcard_buffer_response_delete(VCardBufferResponse *buffer_response);


/*
 * clean up state on reset
 */
void vcard_reset(VCard *card, VCardPower power);

/*
 * applet utilities
 */
/*
 * Constructor for a VCardApplet
 */
VCardApplet *vcard_new_applet(VCardProcessAPDU applet_process_function,
                              VCardResetApplet applet_reset_function,
                              unsigned char *aid, int aid_len);

/*
 * destructor for a VCardApplet
 *  Can be called with a NULL applet
 */
void vcard_delete_applet(VCardApplet *applet);

/* accessor - set the card type specific private data */
void vcard_set_applet_private(VCardApplet *applet, VCardAppletPrivate *_private,
                              VCardAppletPrivateFree private_free);

/* set type of vcard */
void vcard_set_type(VCard *card, VCardType type);

/*
 * utilities interacting with the current applet
 */
/* add a new applet to a card */
VCardStatus vcard_add_applet(VCard *card, VCardApplet *applet);
/* find the applet on the card with the given aid */
VCardApplet *vcard_find_applet(VCard *card, unsigned char *aid, int aid_len);
/* set the following applet to be current on the given channel */
void vcard_select_applet(VCard *card, int channel, VCardApplet *applet);
/* get the card type specific private data on the given channel */
VCardAppletPrivate *vcard_get_current_applet_private(VCard *card, int channel);
/* fetch the applet's id */
unsigned char *vcard_applet_get_aid(VCardApplet *applet, int *aid_len);

/* process the apdu for the current selected applet/file */
VCardStatus vcard_process_applet_apdu(VCard *card, VCardAPDU *apdu,
                                      VCardResponse **response);
/*
 * VCard utilities
 */
/* constructor */
VCard *vcard_new(VCardEmul *_private, VCardEmulFree private_free);
/* get a reference */
VCard *vcard_reference(VCard *);
/* destructor (reference counted) */
void vcard_free(VCard *);
/* get the atr from the card */
void vcard_get_atr(VCard *card, unsigned char *atr, int *atr_len);
void vcard_set_atr_func(VCard *card, VCardGetAtr vcard_get_atr);

/* accessor functions for the response buffer */
VCardBufferResponse *vcard_get_buffer_response(VCard *card);
void vcard_set_buffer_response(VCard *card, VCardBufferResponse *buffer);
/* accessor functions for the type */
VCardType vcard_get_type(VCard *card);
/* get the private data */
VCardEmul *vcard_get_private(VCard *card);

#endif
