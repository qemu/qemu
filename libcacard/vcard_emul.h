/*
 * This is the actual card emulator.
 *
 * These functions can be implemented in different ways on different platforms
 * using the underlying system primitives. For Linux it uses NSS, though direct
 * to PKCS #11, openssl+pkcs11, or even gnu crypto libraries+pkcs #11 could be
 * used. On Windows CAPI could be used.
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#ifndef VCARD_EMUL_H
#define VCARD_EMUL_H 1

#include "card_7816t.h"
#include "vcard.h"
#include "vcard_emul_type.h"

/*
 * types
 */
typedef enum {
    VCARD_EMUL_OK = 0,
    VCARD_EMUL_FAIL,
    /* return values by vcard_emul_init */
    VCARD_EMUL_INIT_ALREADY_INITED,
} VCardEmulError;

/* options are emul specific. call card_emul_parse_args to change a string
 * To an options struct */
typedef struct VCardEmulOptionsStruct VCardEmulOptions;

/*
 * Login functions
 */
/* return the number of login attempts still possible on the card. if unknown,
 * return -1 */
int vcard_emul_get_login_count(VCard *card);
/* login into the card, return the 7816 status word (sw2 || sw1) */
vcard_7816_status_t vcard_emul_login(VCard *card, unsigned char *pin,
                                     int pin_len);

/*
 * key functions
 */
/* delete a key */
void vcard_emul_delete_key(VCardKey *key);
/* RSA sign/decrypt with the key, signature happens 'in place' */
vcard_7816_status_t vcard_emul_rsa_op(VCard *card, VCardKey *key,
                                  unsigned char *buffer, int buffer_size);

void vcard_emul_reset(VCard *card, VCardPower power);
void vcard_emul_get_atr(VCard *card, unsigned char *atr, int *atr_len);

/* Re-insert of a card that has been removed by force removal */
VCardEmulError vcard_emul_force_card_insert(VReader *vreader);
/* Force a card removal even if the card is not physically removed */
VCardEmulError vcard_emul_force_card_remove(VReader *vreader);

VCardEmulOptions *vcard_emul_options(const char *args);
VCardEmulError vcard_emul_init(const VCardEmulOptions *options);
void vcard_emul_replay_insertion_events(void);
void vcard_emul_usage(void);
#endif
