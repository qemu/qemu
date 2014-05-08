/*
 * implement the applets for the CAC card.
 *
 * This code is licensed under the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include "qemu-common.h"

#include "cac.h"
#include "vcard.h"
#include "vcard_emul.h"
#include "card_7816.h"

/* private data for PKI applets */
typedef struct CACPKIAppletDataStruct {
    unsigned char *cert;
    int cert_len;
    unsigned char *cert_buffer;
    int cert_buffer_len;
    unsigned char *sign_buffer;
    int sign_buffer_len;
    VCardKey *key;
} CACPKIAppletData;

/*
 * CAC applet private data
 */
struct VCardAppletPrivateStruct {
    union {
        CACPKIAppletData pki_data;
        void *reserved;
    } u;
};

/*
 * handle all the APDU's that are common to all CAC applets
 */
static VCardStatus
cac_common_process_apdu(VCard *card, VCardAPDU *apdu, VCardResponse **response)
{
    int ef;
    VCardStatus ret = VCARD_FAIL;

    switch (apdu->a_ins) {
    case VCARD7816_INS_SELECT_FILE:
        if (apdu->a_p1 != 0x02) {
            /* let the 7816 code handle applet switches */
            ret = VCARD_NEXT;
            break;
        }
        /* handle file id setting */
        if (apdu->a_Lc != 2) {
            *response = vcard_make_response(
                VCARD7816_STATUS_ERROR_DATA_INVALID);
            ret = VCARD_DONE;
            break;
        }
        /* CAC 1.0 only supports ef = 0 */
        ef = apdu->a_body[0] | (apdu->a_body[1] << 8);
        if (ef != 0) {
            *response = vcard_make_response(
                VCARD7816_STATUS_ERROR_FILE_NOT_FOUND);
            ret = VCARD_DONE;
            break;
        }
        *response = vcard_make_response(VCARD7816_STATUS_SUCCESS);
        ret = VCARD_DONE;
        break;
    case VCARD7816_INS_GET_RESPONSE:
    case VCARD7816_INS_VERIFY:
        /* let the 7816 code handle these */
        ret = VCARD_NEXT;
        break;
    case CAC_GET_PROPERTIES:
    case CAC_GET_ACR:
        /* skip these for now, this will probably be needed */
        *response = vcard_make_response(VCARD7816_STATUS_ERROR_P1_P2_INCORRECT);
        ret = VCARD_DONE;
        break;
    default:
        *response = vcard_make_response(
            VCARD7816_STATUS_ERROR_COMMAND_NOT_SUPPORTED);
        ret = VCARD_DONE;
        break;
    }
    return ret;
}

/*
 *  reset the inter call state between applet selects
 */
static VCardStatus
cac_applet_pki_reset(VCard *card, int channel)
{
    VCardAppletPrivate *applet_private;
    CACPKIAppletData *pki_applet;
    applet_private = vcard_get_current_applet_private(card, channel);
    assert(applet_private);
    pki_applet = &(applet_private->u.pki_data);

    pki_applet->cert_buffer = NULL;
    if (pki_applet->sign_buffer) {
        g_free(pki_applet->sign_buffer);
        pki_applet->sign_buffer = NULL;
    }
    pki_applet->cert_buffer_len = 0;
    pki_applet->sign_buffer_len = 0;
    return VCARD_DONE;
}

static VCardStatus
cac_applet_pki_process_apdu(VCard *card, VCardAPDU *apdu,
                            VCardResponse **response)
{
    CACPKIAppletData *pki_applet;
    VCardAppletPrivate *applet_private;
    int size, next;
    unsigned char *sign_buffer;
    vcard_7816_status_t status;
    VCardStatus ret = VCARD_FAIL;

    applet_private = vcard_get_current_applet_private(card, apdu->a_channel);
    assert(applet_private);
    pki_applet = &(applet_private->u.pki_data);

    switch (apdu->a_ins) {
    case CAC_UPDATE_BUFFER:
        *response = vcard_make_response(
            VCARD7816_STATUS_ERROR_CONDITION_NOT_SATISFIED);
        ret = VCARD_DONE;
        break;
    case CAC_GET_CERTIFICATE:
        if ((apdu->a_p2 != 0) || (apdu->a_p1 != 0)) {
            *response = vcard_make_response(
                             VCARD7816_STATUS_ERROR_P1_P2_INCORRECT);
            break;
        }
        assert(pki_applet->cert != NULL);
        size = apdu->a_Le;
        if (pki_applet->cert_buffer == NULL) {
            pki_applet->cert_buffer = pki_applet->cert;
            pki_applet->cert_buffer_len = pki_applet->cert_len;
        }
        size = MIN(size, pki_applet->cert_buffer_len);
        next = MIN(255, pki_applet->cert_buffer_len - size);
        *response = vcard_response_new_bytes(
                        card, pki_applet->cert_buffer, size,
                        apdu->a_Le, next ?
                        VCARD7816_SW1_WARNING_CHANGE :
                        VCARD7816_SW1_SUCCESS,
                        next);
        pki_applet->cert_buffer += size;
        pki_applet->cert_buffer_len -= size;
        if ((*response == NULL) || (next == 0)) {
            pki_applet->cert_buffer = NULL;
        }
        if (*response == NULL) {
            *response = vcard_make_response(
                            VCARD7816_STATUS_EXC_ERROR_MEMORY_FAILURE);
        }
        ret = VCARD_DONE;
        break;
    case CAC_SIGN_DECRYPT:
        if (apdu->a_p2 != 0) {
            *response = vcard_make_response(
                             VCARD7816_STATUS_ERROR_P1_P2_INCORRECT);
            break;
        }
        size = apdu->a_Lc;

        sign_buffer = g_realloc(pki_applet->sign_buffer,
                                pki_applet->sign_buffer_len + size);
        memcpy(sign_buffer+pki_applet->sign_buffer_len, apdu->a_body, size);
        size += pki_applet->sign_buffer_len;
        switch (apdu->a_p1) {
        case  0x80:
            /* p1 == 0x80 means we haven't yet sent the whole buffer, wait for
             * the rest */
            pki_applet->sign_buffer = sign_buffer;
            pki_applet->sign_buffer_len = size;
            *response = vcard_make_response(VCARD7816_STATUS_SUCCESS);
            break;
        case 0x00:
            /* we now have the whole buffer, do the operation, result will be
             * in the sign_buffer */
            status = vcard_emul_rsa_op(card, pki_applet->key,
                                       sign_buffer, size);
            if (status != VCARD7816_STATUS_SUCCESS) {
                *response = vcard_make_response(status);
                break;
            }
            *response = vcard_response_new(card, sign_buffer, size, apdu->a_Le,
                                                     VCARD7816_STATUS_SUCCESS);
            if (*response == NULL) {
                *response = vcard_make_response(
                                VCARD7816_STATUS_EXC_ERROR_MEMORY_FAILURE);
            }
            break;
        default:
           *response = vcard_make_response(
                                VCARD7816_STATUS_ERROR_P1_P2_INCORRECT);
            break;
        }
        g_free(sign_buffer);
        pki_applet->sign_buffer = NULL;
        pki_applet->sign_buffer_len = 0;
        ret = VCARD_DONE;
        break;
    case CAC_READ_BUFFER:
        /* new CAC call, go ahead and use the old version for now */
        /* TODO: implement */
        *response = vcard_make_response(
                                VCARD7816_STATUS_ERROR_COMMAND_NOT_SUPPORTED);
        ret = VCARD_DONE;
        break;
    default:
        ret = cac_common_process_apdu(card, apdu, response);
        break;
    }
    return ret;
}


static VCardStatus
cac_applet_id_process_apdu(VCard *card, VCardAPDU *apdu,
                           VCardResponse **response)
{
    VCardStatus ret = VCARD_FAIL;

    switch (apdu->a_ins) {
    case CAC_UPDATE_BUFFER:
        *response = vcard_make_response(
                        VCARD7816_STATUS_ERROR_CONDITION_NOT_SATISFIED);
        ret = VCARD_DONE;
        break;
    case CAC_READ_BUFFER:
        /* new CAC call, go ahead and use the old version for now */
        /* TODO: implement */
        *response = vcard_make_response(
                        VCARD7816_STATUS_ERROR_COMMAND_NOT_SUPPORTED);
        ret = VCARD_DONE;
        break;
    default:
        ret = cac_common_process_apdu(card, apdu, response);
        break;
    }
    return ret;
}


/*
 * TODO: if we ever want to support general CAC middleware, we will need to
 * implement the various containers.
 */
static VCardStatus
cac_applet_container_process_apdu(VCard *card, VCardAPDU *apdu,
                                  VCardResponse **response)
{
    VCardStatus ret = VCARD_FAIL;

    switch (apdu->a_ins) {
    case CAC_READ_BUFFER:
    case CAC_UPDATE_BUFFER:
        *response = vcard_make_response(
                        VCARD7816_STATUS_ERROR_COMMAND_NOT_SUPPORTED);
        ret = VCARD_DONE;
        break;
    default:
        ret = cac_common_process_apdu(card, apdu, response);
        break;
    }
    return ret;
}

/*
 * utilities for creating and destroying the private applet data
 */
static void
cac_delete_pki_applet_private(VCardAppletPrivate *applet_private)
{
    CACPKIAppletData *pki_applet_data;

    if (applet_private == NULL) {
        return;
    }
    pki_applet_data = &(applet_private->u.pki_data);
    if (pki_applet_data->cert != NULL) {
        g_free(pki_applet_data->cert);
    }
    if (pki_applet_data->sign_buffer != NULL) {
        g_free(pki_applet_data->sign_buffer);
    }
    if (pki_applet_data->key != NULL) {
        vcard_emul_delete_key(pki_applet_data->key);
    }
    g_free(applet_private);
}

static VCardAppletPrivate *
cac_new_pki_applet_private(const unsigned char *cert,
                           int cert_len, VCardKey *key)
{
    CACPKIAppletData *pki_applet_data;
    VCardAppletPrivate *applet_private;

    applet_private = g_new0(VCardAppletPrivate, 1);
    pki_applet_data = &(applet_private->u.pki_data);
    pki_applet_data->cert = (unsigned char *)g_malloc(cert_len+1);
    /*
     * if we want to support compression, then we simply change the 0 to a 1
     * and compress the cert data with libz
     */
    pki_applet_data->cert[0] = 0; /* not compressed */
    memcpy(&pki_applet_data->cert[1], cert, cert_len);
    pki_applet_data->cert_len = cert_len+1;

    pki_applet_data->key = key;
    return applet_private;
}


/*
 * create a new cac applet which links to a given cert
 */
static VCardApplet *
cac_new_pki_applet(int i, const unsigned char *cert,
                   int cert_len, VCardKey *key)
{
    VCardAppletPrivate *applet_private;
    VCardApplet *applet;
    unsigned char pki_aid[] = { 0xa0, 0x00, 0x00, 0x00, 0x79, 0x01, 0x00 };
    int pki_aid_len = sizeof(pki_aid);

    pki_aid[pki_aid_len-1] = i;

    applet_private = cac_new_pki_applet_private(cert, cert_len, key);
    if (applet_private == NULL) {
        goto failure;
    }
    applet = vcard_new_applet(cac_applet_pki_process_apdu, cac_applet_pki_reset,
                              pki_aid, pki_aid_len);
    if (applet == NULL) {
        goto failure;
    }
    vcard_set_applet_private(applet, applet_private,
                             cac_delete_pki_applet_private);
    applet_private = NULL;

    return applet;

failure:
    if (applet_private != NULL) {
        cac_delete_pki_applet_private(applet_private);
    }
    return NULL;
}


static unsigned char cac_default_container_aid[] = {
    0xa0, 0x00, 0x00, 0x00, 0x30, 0x00, 0x00 };
static unsigned char cac_id_aid[] = {
    0xa0, 0x00, 0x00, 0x00, 0x79, 0x03, 0x00 };
/*
 * Initialize the cac card. This is the only public function in this file. All
 * the rest are connected through function pointers.
 */
VCardStatus
cac_card_init(VReader *reader, VCard *card,
              const char *params,
              unsigned char * const *cert,
              int cert_len[],
              VCardKey *key[] /* adopt the keys*/,
              int cert_count)
{
    int i;
    VCardApplet *applet;

    /* CAC Cards are VM Cards */
    vcard_set_type(card, VCARD_VM);

    /* create one PKI applet for each cert */
    for (i = 0; i < cert_count; i++) {
        applet = cac_new_pki_applet(i, cert[i], cert_len[i], key[i]);
        if (applet == NULL) {
            goto failure;
        }
        vcard_add_applet(card, applet);
    }

    /* create a default blank container applet */
    applet = vcard_new_applet(cac_applet_container_process_apdu,
                              NULL, cac_default_container_aid,
                              sizeof(cac_default_container_aid));
    if (applet == NULL) {
        goto failure;
    }
    vcard_add_applet(card, applet);

    /* create a default blank container applet */
    applet = vcard_new_applet(cac_applet_id_process_apdu,
                              NULL, cac_id_aid,
                              sizeof(cac_id_aid));
    if (applet == NULL) {
        goto failure;
    }
    vcard_add_applet(card, applet);
    return VCARD_DONE;

failure:
    return VCARD_FAIL;
}

