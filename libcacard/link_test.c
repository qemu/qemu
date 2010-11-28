/*
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include <stdio.h>
#include "vcard.h"

VCardStatus cac_card_init(const char *flags, VCard *card,
                const unsigned char *cert[],
                int cert_len[], VCardKey *key[] /* adopt the keys*/,
                int cert_count);
/*
 * this will crash... just test the linkage right now
 */

main(int argc, char **argv)
{
    VCard *card; /* no constructor yet */
    cac_card_init("", card, NULL, 0, NULL, 0);
}

