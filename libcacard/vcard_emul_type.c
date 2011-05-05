/*
 *  This file contains utility functions which abstract the different card
 *  types.  The goal is that new card types can easily be added by simply
 *  changing this file and vcard_emul_type.h. It is currently not a requirement
 *  to dynamically add new card types.
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include <strings.h>
#include "vcardt.h"
#include "vcard_emul_type.h"
#include "cac.h"

VCardStatus vcard_init(VReader *vreader, VCard *vcard,
                       VCardEmulType type, const char *params,
                       unsigned char *const *cert, int cert_len[],
                       VCardKey *key[], int cert_count)
{
    switch (type) {
    case VCARD_EMUL_NONE:
        break;
    case VCARD_EMUL_CAC:
        return cac_card_init(vreader, vcard, params,
                             cert, cert_len, key,  cert_count);
    /* add new ones here */
    default:
        break;
    }
    return VCARD_FAIL;
}

VCardEmulType vcard_emul_type_select(VReader *vreader)
{
#ifdef notdef
    /* since there is only one emulator no need to call this function */
    if (cac_is_cac_card(vreader) == VCARD_DONE) {
        return VCARD_EMUL_CAC;
    }
#endif
    /* return the default */
    return VCARD_EMUL_CAC;
}

VCardEmulType vcard_emul_type_from_string(const char *type_string)
{
     if (strcasecmp(type_string, "CAC") == 0) {
        return VCARD_EMUL_CAC;
     }
#ifdef USE_PASSTHRU
     if (strcasecmp(type_string, "PASSTHRU") == 0) {
        return VCARD_EMUL_PASSTHRU;
     }
#endif
     return VCARD_EMUL_NONE;
}
