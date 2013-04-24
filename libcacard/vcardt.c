#include <stdlib.h>
#include <string.h>
#include <glib.h>

#include "libcacard/vcardt.h"

#include "libcacard/vcardt_internal.h"

/* create an ATR with appropriate historical bytes */
#define ATR_TS_DIRECT_CONVENTION 0x3b
#define ATR_TA_PRESENT 0x10
#define ATR_TB_PRESENT 0x20
#define ATR_TC_PRESENT 0x40
#define ATR_TD_PRESENT 0x80

unsigned char *vcard_alloc_atr(const char *postfix, int *atr_len)
{
    int postfix_len;
    const char prefix[] = "VCARD_";
    const char default_postfix[] = "DEFAULT";
    const int prefix_len = sizeof(prefix) - 1;
    int total_len;
    unsigned char *atr;

    if (postfix == NULL) {
        postfix = default_postfix;
    }
    postfix_len = strlen(postfix);
    total_len = 3 + prefix_len + postfix_len;
    atr = g_malloc(total_len);
    atr[0] = ATR_TS_DIRECT_CONVENTION;
    atr[1] = ATR_TD_PRESENT + prefix_len + postfix_len;
    atr[2] = 0x00;
    memcpy(&atr[3], prefix, prefix_len);
    memcpy(&atr[3 + prefix_len], postfix, postfix_len);
    if (atr_len) {
        *atr_len = total_len;
    }
    return atr;
}
