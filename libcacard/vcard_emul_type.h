/*
 *  This header file abstracts the different card types. The goal is new card
 *  types can easily be added by simply changing this file and
 *  vcard_emul_type.c. It is currently not a requirement to dynamically add new
 *  card types.
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#ifndef VCARD_EMUL_TYPE_H
#define VCARD_EMUL_TYPE_H 1
#include "vcardt.h"
#include "vreadert.h"

/*
 * types
 */
typedef enum {
     VCARD_EMUL_NONE = 0,
     VCARD_EMUL_CAC,
     VCARD_EMUL_PASSTHRU
} VCardEmulType;

/* functions used by the rest of the emulator */
VCardStatus vcard_init(VReader *vreader, VCard *vcard, VCardEmulType type,
                       const char *params, unsigned char * const *cert,
                       int cert_len[], VCardKey *key[], int cert_count);
VCardEmulType vcard_emul_type_select(VReader *vreader);
VCardEmulType vcard_emul_type_from_string(const char *type_string);

#endif
