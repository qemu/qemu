/*
 * Implement the 7816 portion of the card spec
 *
 * This code is licensed under the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include "qemu-common.h"

#include "vcard.h"
#include "vcard_emul.h"
#include "card_7816.h"

/*
 * set the status bytes based on the status word
 */
static void
vcard_response_set_status(VCardResponse *response, vcard_7816_status_t status)
{
    unsigned char sw1, sw2;
    response->b_status = status; /* make sure the status and swX representations
                                  * are consistent */
    sw1 = (status >> 8) & 0xff;
    sw2 = status & 0xff;
    response->b_sw1 = sw1;
    response->b_sw2 = sw2;
    response->b_data[response->b_len] = sw1;
    response->b_data[response->b_len+1] = sw2;
}

/*
 * set the status bytes in a response buffer
 */
static void
vcard_response_set_status_bytes(VCardResponse *response,
                               unsigned char sw1, unsigned char sw2)
{
    response->b_status = sw1 << 8 | sw2;
    response->b_sw1 = sw1;
    response->b_sw2 = sw2;
    response->b_data[response->b_len] = sw1;
    response->b_data[response->b_len+1] = sw2;
}

/*
 * allocate a VCardResponse structure, plus space for the data buffer, and
 * set up everything but the resonse bytes.
 */
VCardResponse *
vcard_response_new_data(unsigned char *buf, int len)
{
    VCardResponse *new_response;

    new_response = (VCardResponse *)qemu_malloc(sizeof(VCardResponse));
    new_response->b_data = qemu_malloc(len + 2);
    memcpy(new_response->b_data, buf, len);
    new_response->b_total_len = len+2;
    new_response->b_len = len;
    new_response->b_type = VCARD_MALLOC;
    return new_response;
}

static VCardResponse *
vcard_init_buffer_response(VCard *card, unsigned char *buf, int len)
{
    VCardResponse *response;
    VCardBufferResponse *buffer_response;

    buffer_response = vcard_get_buffer_response(card);
    if (buffer_response) {
        vcard_set_buffer_response(card, NULL);
        vcard_buffer_response_delete(buffer_response);
    }
    buffer_response = vcard_buffer_response_new(buf, len);
    if (buffer_response == NULL) {
        return NULL;
    }
    response = vcard_response_new_status_bytes(VCARD7816_SW1_RESPONSE_BYTES,
                                               len > 255 ? 0 : len);
    if (response == NULL) {
        return NULL;
    }
    vcard_set_buffer_response(card, buffer_response);
    return response;
}

/*
 * general buffer to hold results from APDU calls
 */
VCardResponse *
vcard_response_new(VCard *card, unsigned char *buf,
                   int len, int Le, vcard_7816_status_t status)
{
    VCardResponse *new_response;

    if (len > Le) {
        return vcard_init_buffer_response(card, buf, len);
    }
    new_response = vcard_response_new_data(buf, len);
    if (new_response == NULL) {
        return NULL;
    }
    vcard_response_set_status(new_response, status);
    return new_response;
}

/*
 * general buffer to hold results from APDU calls
 */
VCardResponse *
vcard_response_new_bytes(VCard *card, unsigned char *buf, int len, int Le,
                         unsigned char sw1, unsigned char sw2)
{
    VCardResponse *new_response;

    if (len > Le) {
        return vcard_init_buffer_response(card, buf, len);
    }
    new_response = vcard_response_new_data(buf, len);
    if (new_response == NULL) {
        return NULL;
    }
    vcard_response_set_status_bytes(new_response, sw1, sw2);
    return new_response;
}

/*
 * get a new Reponse buffer that only has a status.
 */
static VCardResponse *
vcard_response_new_status(vcard_7816_status_t status)
{
    VCardResponse *new_response;

    new_response = (VCardResponse *)qemu_malloc(sizeof(VCardResponse));
    new_response->b_data = &new_response->b_sw1;
    new_response->b_len = 0;
    new_response->b_total_len = 2;
    new_response->b_type = VCARD_MALLOC_STRUCT;
    vcard_response_set_status(new_response, status);
    return new_response;
}

/*
 * same as above, but specify the status as separate bytes
 */
VCardResponse *
vcard_response_new_status_bytes(unsigned char sw1, unsigned char sw2)
{
    VCardResponse *new_response;

    new_response = (VCardResponse *)qemu_malloc(sizeof(VCardResponse));
    new_response->b_data = &new_response->b_sw1;
    new_response->b_len = 0;
    new_response->b_total_len = 2;
    new_response->b_type = VCARD_MALLOC_STRUCT;
    vcard_response_set_status_bytes(new_response, sw1, sw2);
    return new_response;
}


/*
 * free the response buffer. The Buffer has a type to handle the buffer
 * allocated in other ways than through malloc.
 */
void
vcard_response_delete(VCardResponse *response)
{
    if (response == NULL) {
        return;
    }
    switch (response->b_type) {
    case VCARD_MALLOC:
        /* everything was malloc'ed */
        if (response->b_data) {
            qemu_free(response->b_data);
        }
        qemu_free(response);
        break;
    case VCARD_MALLOC_DATA:
        /* only the data buffer was malloc'ed */
        if (response->b_data) {
            qemu_free(response->b_data);
        }
        break;
    case VCARD_MALLOC_STRUCT:
        /* only the structure was malloc'ed */
        qemu_free(response);
        break;
    case VCARD_STATIC:
        break;
    }
}

/*
 * decode the class bit and set our generic type field, channel, and
 * secure messaging values.
 */
static vcard_7816_status_t
vcard_apdu_set_class(VCardAPDU *apdu) {
    apdu->a_channel = 0;
    apdu->a_secure_messaging = 0;
    apdu->a_type = apdu->a_cla & 0xf0;
    apdu->a_gen_type = VCARD_7816_ISO;

    /* parse the class  tables 8 & 9 of the 7816-4 Part 4 spec */
    switch (apdu->a_type) {
        /* we only support the basic types */
    case 0x00:
    case 0x80:
    case 0x90:
    case 0xa0:
        apdu->a_channel = apdu->a_cla & 3;
        apdu->a_secure_messaging = apdu->a_cla & 0xe;
        break;
    case 0xb0:
    case 0xc0:
        break;

    case 0x10:
    case 0x20:
    case 0x30:
    case 0x40:
    case 0x50:
    case 0x60:
    case 0x70:
        /* Reserved for future use */
        apdu->a_gen_type = VCARD_7816_RFU;
        break;
    case 0xd0:
    case 0xe0:
    case 0xf0:
    default:
        apdu->a_gen_type =
            (apdu->a_cla == 0xff) ? VCARD_7816_PTS : VCARD_7816_PROPIETARY;
        break;
    }
    return VCARD7816_STATUS_SUCCESS;
}

/*
 * set the Le and Lc fiels according to table 5 of the
 * 7816-4 part 4 spec
 */
static vcard_7816_status_t
vcard_apdu_set_length(VCardAPDU *apdu)
{
    int L, Le;

    /* process according to table 5 of the 7816-4 Part 4 spec.
     * variable names match the variables in the spec */
    L = apdu->a_len-4; /* fixed APDU header */
    apdu->a_Lc = 0;
    apdu->a_Le = 0;
    apdu->a_body = NULL;
    switch (L) {
    case 0:
        /* 1 minimal apdu */
        return VCARD7816_STATUS_SUCCESS;
    case 1:
        /* 2S only return values apdu */
        /*   zero maps to 256 here */
        apdu->a_Le = apdu->a_header->ah_Le ?
                         apdu->a_header->ah_Le : 256;
        return VCARD7816_STATUS_SUCCESS;
    default:
        /* if the ah_Le byte is zero and we have more than
         * 1 byte in the header, then we must be using extended Le and Lc.
         * process the extended now. */
        if (apdu->a_header->ah_Le == 0) {
            if (L < 3) {
                /* coding error, need at least 3 bytes */
                return VCARD7816_STATUS_ERROR_WRONG_LENGTH;
            }
            /* calculate the first extended value. Could be either Le or Lc */
            Le = (apdu->a_header->ah_body[0] << 8)
               || apdu->a_header->ah_body[1];
            if (L == 3) {
                /* 2E extended, return data only */
                /*   zero maps to 65536 */
                apdu->a_Le = Le ? Le : 65536;
                return VCARD7816_STATUS_SUCCESS;
            }
            if (Le == 0) {
                /* reserved for future use, probably for next time we need
                 * to extend the lengths */
                return VCARD7816_STATUS_ERROR_WRONG_LENGTH;
            }
            /* we know that the first extended value is Lc now */
            apdu->a_Lc = Le;
            apdu->a_body = &apdu->a_header->ah_body[2];
            if (L == Le+3) {
                /* 3E extended, only body parameters */
                return VCARD7816_STATUS_SUCCESS;
            }
            if (L == Le+5) {
                /* 4E extended, parameters and return data */
                Le = (apdu->a_data[apdu->a_len-2] << 8)
                   || apdu->a_data[apdu->a_len-1];
                apdu->a_Le = Le ? Le : 65536;
                return VCARD7816_STATUS_SUCCESS;
            }
            return VCARD7816_STATUS_ERROR_WRONG_LENGTH;
        }
        /* not extended */
        apdu->a_Lc = apdu->a_header->ah_Le;
        apdu->a_body = &apdu->a_header->ah_body[0];
        if (L ==  apdu->a_Lc + 1) {
            /* 3S only body parameters */
            return VCARD7816_STATUS_SUCCESS;
        }
        if (L ==  apdu->a_Lc + 2) {
            /* 4S parameters and return data */
            Le = apdu->a_data[apdu->a_len-1];
            apdu->a_Le = Le ?  Le : 256;
            return VCARD7816_STATUS_SUCCESS;
        }
        break;
    }
    return VCARD7816_STATUS_ERROR_WRONG_LENGTH;
}

/*
 * create a new APDU from a raw set of bytes. This will decode all the
 * above fields. users of VCARDAPDU's can then depend on the already decoded
 * values.
 */
VCardAPDU *
vcard_apdu_new(unsigned char *raw_apdu, int len, vcard_7816_status_t *status)
{
    VCardAPDU *new_apdu;

    *status = VCARD7816_STATUS_EXC_ERROR_MEMORY_FAILURE;
    if (len < 4) {
        *status = VCARD7816_STATUS_ERROR_WRONG_LENGTH;
        return NULL;
    }

    new_apdu = (VCardAPDU *)qemu_malloc(sizeof(VCardAPDU));
    new_apdu->a_data = qemu_malloc(len);
    memcpy(new_apdu->a_data, raw_apdu, len);
    new_apdu->a_len = len;
    *status = vcard_apdu_set_class(new_apdu);
    if (*status != VCARD7816_STATUS_SUCCESS) {
        qemu_free(new_apdu);
        return NULL;
    }
    *status = vcard_apdu_set_length(new_apdu);
    if (*status != VCARD7816_STATUS_SUCCESS) {
        qemu_free(new_apdu);
        new_apdu = NULL;
    }
    return new_apdu;
}

void
vcard_apdu_delete(VCardAPDU *apdu)
{
    if (apdu == NULL) {
        return;
    }
    if (apdu->a_data) {
        qemu_free(apdu->a_data);
    }
    qemu_free(apdu);
}


/*
 * declare response buffers for all the 7816 defined error codes
 */
VCARD_RESPONSE_NEW_STATIC_STATUS(VCARD7816_STATUS_SUCCESS)
VCARD_RESPONSE_NEW_STATIC_STATUS(VCARD7816_STATUS_WARNING)
VCARD_RESPONSE_NEW_STATIC_STATUS(VCARD7816_STATUS_WARNING_RET_CORUPT)
VCARD_RESPONSE_NEW_STATIC_STATUS(VCARD7816_STATUS_WARNING_BUF_END_BEFORE_LE)
VCARD_RESPONSE_NEW_STATIC_STATUS(VCARD7816_STATUS_WARNING_INVALID_FILE_SELECTED)
VCARD_RESPONSE_NEW_STATIC_STATUS(VCARD7816_STATUS_WARNING_FCI_FORMAT_INVALID)
VCARD_RESPONSE_NEW_STATIC_STATUS(VCARD7816_STATUS_WARNING_CHANGE)
VCARD_RESPONSE_NEW_STATIC_STATUS(VCARD7816_STATUS_WARNING_FILE_FILLED)
VCARD_RESPONSE_NEW_STATIC_STATUS(VCARD7816_STATUS_EXC_ERROR)
VCARD_RESPONSE_NEW_STATIC_STATUS(VCARD7816_STATUS_EXC_ERROR_CHANGE)
VCARD_RESPONSE_NEW_STATIC_STATUS(VCARD7816_STATUS_EXC_ERROR_MEMORY_FAILURE)
VCARD_RESPONSE_NEW_STATIC_STATUS(VCARD7816_STATUS_ERROR_WRONG_LENGTH)
VCARD_RESPONSE_NEW_STATIC_STATUS(VCARD7816_STATUS_ERROR_CLA_NOT_SUPPORTED)
VCARD_RESPONSE_NEW_STATIC_STATUS(VCARD7816_STATUS_ERROR_CHANNEL_NOT_SUPPORTED)
VCARD_RESPONSE_NEW_STATIC_STATUS(VCARD7816_STATUS_ERROR_SECURE_NOT_SUPPORTED)
VCARD_RESPONSE_NEW_STATIC_STATUS(VCARD7816_STATUS_ERROR_COMMAND_NOT_SUPPORTED)
VCARD_RESPONSE_NEW_STATIC_STATUS(
                    VCARD7816_STATUS_ERROR_COMMAND_INCOMPATIBLE_WITH_FILE)
VCARD_RESPONSE_NEW_STATIC_STATUS(VCARD7816_STATUS_ERROR_SECURITY_NOT_SATISFIED)
VCARD_RESPONSE_NEW_STATIC_STATUS(VCARD7816_STATUS_ERROR_AUTHENTICATION_BLOCKED)
VCARD_RESPONSE_NEW_STATIC_STATUS(VCARD7816_STATUS_ERROR_DATA_INVALID)
VCARD_RESPONSE_NEW_STATIC_STATUS(VCARD7816_STATUS_ERROR_CONDITION_NOT_SATISFIED)
VCARD_RESPONSE_NEW_STATIC_STATUS(VCARD7816_STATUS_ERROR_DATA_NO_EF)
VCARD_RESPONSE_NEW_STATIC_STATUS(VCARD7816_STATUS_ERROR_SM_OBJECT_MISSING)
VCARD_RESPONSE_NEW_STATIC_STATUS(VCARD7816_STATUS_ERROR_SM_OBJECT_INCORRECT)
VCARD_RESPONSE_NEW_STATIC_STATUS(VCARD7816_STATUS_ERROR_WRONG_PARAMETERS)
VCARD_RESPONSE_NEW_STATIC_STATUS(
                            VCARD7816_STATUS_ERROR_WRONG_PARAMETERS_IN_DATA)
VCARD_RESPONSE_NEW_STATIC_STATUS(VCARD7816_STATUS_ERROR_FUNCTION_NOT_SUPPORTED)
VCARD_RESPONSE_NEW_STATIC_STATUS(VCARD7816_STATUS_ERROR_FILE_NOT_FOUND)
VCARD_RESPONSE_NEW_STATIC_STATUS(VCARD7816_STATUS_ERROR_RECORD_NOT_FOUND)
VCARD_RESPONSE_NEW_STATIC_STATUS(VCARD7816_STATUS_ERROR_NO_SPACE_FOR_FILE)
VCARD_RESPONSE_NEW_STATIC_STATUS(VCARD7816_STATUS_ERROR_LC_TLV_INCONSISTENT)
VCARD_RESPONSE_NEW_STATIC_STATUS(VCARD7816_STATUS_ERROR_P1_P2_INCORRECT)
VCARD_RESPONSE_NEW_STATIC_STATUS(VCARD7816_STATUS_ERROR_LC_P1_P2_INCONSISTENT)
VCARD_RESPONSE_NEW_STATIC_STATUS(VCARD7816_STATUS_ERROR_DATA_NOT_FOUND)
VCARD_RESPONSE_NEW_STATIC_STATUS(VCARD7816_STATUS_ERROR_WRONG_PARAMETERS_2)
VCARD_RESPONSE_NEW_STATIC_STATUS(VCARD7816_STATUS_ERROR_INS_CODE_INVALID)
VCARD_RESPONSE_NEW_STATIC_STATUS(VCARD7816_STATUS_ERROR_CLA_INVALID)
VCARD_RESPONSE_NEW_STATIC_STATUS(VCARD7816_STATUS_ERROR_GENERAL)

/*
 * return a single response code. This function cannot fail. It will always
 * return a response.
 */
VCardResponse *
vcard_make_response(vcard_7816_status_t status)
{
    VCardResponse *response = NULL;

    switch (status) {
    /* known 7816 response codes */
    case VCARD7816_STATUS_SUCCESS:
        return VCARD_RESPONSE_GET_STATIC(
                    VCARD7816_STATUS_SUCCESS);
    case VCARD7816_STATUS_WARNING:
        return VCARD_RESPONSE_GET_STATIC(
                    VCARD7816_STATUS_WARNING);
    case VCARD7816_STATUS_WARNING_RET_CORUPT:
        return VCARD_RESPONSE_GET_STATIC(
                    VCARD7816_STATUS_WARNING_RET_CORUPT);
    case VCARD7816_STATUS_WARNING_BUF_END_BEFORE_LE:
        return VCARD_RESPONSE_GET_STATIC(
                    VCARD7816_STATUS_WARNING_BUF_END_BEFORE_LE);
    case VCARD7816_STATUS_WARNING_INVALID_FILE_SELECTED:
        return VCARD_RESPONSE_GET_STATIC(
                    VCARD7816_STATUS_WARNING_INVALID_FILE_SELECTED);
    case VCARD7816_STATUS_WARNING_FCI_FORMAT_INVALID:
        return VCARD_RESPONSE_GET_STATIC(
                    VCARD7816_STATUS_WARNING_FCI_FORMAT_INVALID);
    case VCARD7816_STATUS_WARNING_CHANGE:
        return VCARD_RESPONSE_GET_STATIC(
                    VCARD7816_STATUS_WARNING_CHANGE);
    case VCARD7816_STATUS_WARNING_FILE_FILLED:
        return VCARD_RESPONSE_GET_STATIC(
                    VCARD7816_STATUS_WARNING_FILE_FILLED);
    case VCARD7816_STATUS_EXC_ERROR:
        return VCARD_RESPONSE_GET_STATIC(
                    VCARD7816_STATUS_EXC_ERROR);
    case VCARD7816_STATUS_EXC_ERROR_CHANGE:
        return VCARD_RESPONSE_GET_STATIC(
                    VCARD7816_STATUS_EXC_ERROR_CHANGE);
    case VCARD7816_STATUS_EXC_ERROR_MEMORY_FAILURE:
        return VCARD_RESPONSE_GET_STATIC(
                    VCARD7816_STATUS_EXC_ERROR_MEMORY_FAILURE);
    case VCARD7816_STATUS_ERROR_WRONG_LENGTH:
        return VCARD_RESPONSE_GET_STATIC(
                    VCARD7816_STATUS_ERROR_WRONG_LENGTH);
    case VCARD7816_STATUS_ERROR_CLA_NOT_SUPPORTED:
        return VCARD_RESPONSE_GET_STATIC(
                    VCARD7816_STATUS_ERROR_CLA_NOT_SUPPORTED);
    case VCARD7816_STATUS_ERROR_CHANNEL_NOT_SUPPORTED:
        return VCARD_RESPONSE_GET_STATIC(
                    VCARD7816_STATUS_ERROR_CHANNEL_NOT_SUPPORTED);
    case VCARD7816_STATUS_ERROR_SECURE_NOT_SUPPORTED:
        return VCARD_RESPONSE_GET_STATIC(
                    VCARD7816_STATUS_ERROR_SECURE_NOT_SUPPORTED);
    case VCARD7816_STATUS_ERROR_COMMAND_NOT_SUPPORTED:
        return VCARD_RESPONSE_GET_STATIC(
                    VCARD7816_STATUS_ERROR_COMMAND_NOT_SUPPORTED);
    case VCARD7816_STATUS_ERROR_COMMAND_INCOMPATIBLE_WITH_FILE:
        return VCARD_RESPONSE_GET_STATIC(
                    VCARD7816_STATUS_ERROR_COMMAND_INCOMPATIBLE_WITH_FILE);
    case VCARD7816_STATUS_ERROR_SECURITY_NOT_SATISFIED:
        return VCARD_RESPONSE_GET_STATIC(
                    VCARD7816_STATUS_ERROR_SECURITY_NOT_SATISFIED);
    case VCARD7816_STATUS_ERROR_AUTHENTICATION_BLOCKED:
        return VCARD_RESPONSE_GET_STATIC(
                    VCARD7816_STATUS_ERROR_AUTHENTICATION_BLOCKED);
    case VCARD7816_STATUS_ERROR_DATA_INVALID:
        return VCARD_RESPONSE_GET_STATIC(
                    VCARD7816_STATUS_ERROR_DATA_INVALID);
    case VCARD7816_STATUS_ERROR_CONDITION_NOT_SATISFIED:
        return VCARD_RESPONSE_GET_STATIC(
                    VCARD7816_STATUS_ERROR_CONDITION_NOT_SATISFIED);
    case VCARD7816_STATUS_ERROR_DATA_NO_EF:
        return VCARD_RESPONSE_GET_STATIC(
                    VCARD7816_STATUS_ERROR_DATA_NO_EF);
    case VCARD7816_STATUS_ERROR_SM_OBJECT_MISSING:
        return VCARD_RESPONSE_GET_STATIC(
                    VCARD7816_STATUS_ERROR_SM_OBJECT_MISSING);
    case VCARD7816_STATUS_ERROR_SM_OBJECT_INCORRECT:
        return VCARD_RESPONSE_GET_STATIC(
                    VCARD7816_STATUS_ERROR_SM_OBJECT_INCORRECT);
    case VCARD7816_STATUS_ERROR_WRONG_PARAMETERS:
        return VCARD_RESPONSE_GET_STATIC(
                    VCARD7816_STATUS_ERROR_WRONG_PARAMETERS);
    case VCARD7816_STATUS_ERROR_WRONG_PARAMETERS_IN_DATA:
        return VCARD_RESPONSE_GET_STATIC(
                    VCARD7816_STATUS_ERROR_WRONG_PARAMETERS_IN_DATA);
    case VCARD7816_STATUS_ERROR_FUNCTION_NOT_SUPPORTED:
        return VCARD_RESPONSE_GET_STATIC(
                    VCARD7816_STATUS_ERROR_FUNCTION_NOT_SUPPORTED);
    case VCARD7816_STATUS_ERROR_FILE_NOT_FOUND:
        return VCARD_RESPONSE_GET_STATIC(
                    VCARD7816_STATUS_ERROR_FILE_NOT_FOUND);
    case VCARD7816_STATUS_ERROR_RECORD_NOT_FOUND:
        return VCARD_RESPONSE_GET_STATIC(
                    VCARD7816_STATUS_ERROR_RECORD_NOT_FOUND);
    case VCARD7816_STATUS_ERROR_NO_SPACE_FOR_FILE:
        return VCARD_RESPONSE_GET_STATIC(
                    VCARD7816_STATUS_ERROR_NO_SPACE_FOR_FILE);
    case VCARD7816_STATUS_ERROR_LC_TLV_INCONSISTENT:
        return VCARD_RESPONSE_GET_STATIC(
                    VCARD7816_STATUS_ERROR_LC_TLV_INCONSISTENT);
    case VCARD7816_STATUS_ERROR_P1_P2_INCORRECT:
        return VCARD_RESPONSE_GET_STATIC(
                    VCARD7816_STATUS_ERROR_P1_P2_INCORRECT);
    case VCARD7816_STATUS_ERROR_LC_P1_P2_INCONSISTENT:
        return VCARD_RESPONSE_GET_STATIC(
                    VCARD7816_STATUS_ERROR_LC_P1_P2_INCONSISTENT);
    case VCARD7816_STATUS_ERROR_DATA_NOT_FOUND:
        return VCARD_RESPONSE_GET_STATIC(
                    VCARD7816_STATUS_ERROR_DATA_NOT_FOUND);
    case VCARD7816_STATUS_ERROR_WRONG_PARAMETERS_2:
        return VCARD_RESPONSE_GET_STATIC(
                    VCARD7816_STATUS_ERROR_WRONG_PARAMETERS_2);
    case VCARD7816_STATUS_ERROR_INS_CODE_INVALID:
        return VCARD_RESPONSE_GET_STATIC(
                    VCARD7816_STATUS_ERROR_INS_CODE_INVALID);
    case VCARD7816_STATUS_ERROR_CLA_INVALID:
        return VCARD_RESPONSE_GET_STATIC(
                    VCARD7816_STATUS_ERROR_CLA_INVALID);
    case VCARD7816_STATUS_ERROR_GENERAL:
        return VCARD_RESPONSE_GET_STATIC(
                    VCARD7816_STATUS_ERROR_GENERAL);
    default:
        /* we don't know this status code, create a response buffer to
         * hold it */
        response = vcard_response_new_status(status);
        if (response == NULL) {
            /* couldn't allocate the buffer, return memmory error */
            return VCARD_RESPONSE_GET_STATIC(
                        VCARD7816_STATUS_EXC_ERROR_MEMORY_FAILURE);
        }
    }
    assert(response);
    return response;
}

/*
 * Add File card support here if you need it.
 */
static VCardStatus
vcard7816_file_system_process_apdu(VCard *card, VCardAPDU *apdu,
                                   VCardResponse **response)
{
    /* TODO: if we want to support a virtual file system card, we do it here.
     * It would probably be a pkcs #15 card type */
    *response = vcard_make_response(
                    VCARD7816_STATUS_ERROR_COMMAND_NOT_SUPPORTED);
    return VCARD_DONE;
}

/*
 * VM card (including java cards)
 */
static VCardStatus
vcard7816_vm_process_apdu(VCard *card, VCardAPDU *apdu,
                          VCardResponse **response)
{
    int bytes_to_copy, next_byte_count, count;
    VCardApplet *current_applet;
    VCardBufferResponse *buffer_response;
    vcard_7816_status_t status;

    /* parse the class first */
    if (apdu->a_gen_type !=  VCARD_7816_ISO) {
        *response = vcard_make_response(
                        VCARD7816_STATUS_ERROR_COMMAND_NOT_SUPPORTED);
        return VCARD_DONE;
    }

    /* use a switch so that if we need to support secure channel stuff later,
     * we know where to put it */
    switch (apdu->a_secure_messaging) {
    case 0x0: /* no SM */
        break;
    case 0x4: /* proprietary SM */
    case 0x8: /* header not authenticated */
    case 0xc: /* header authenticated */
    default:
        /* for now, don't try to support secure channel stuff in the
         * virtual card. */
        *response = vcard_make_response(
                        VCARD7816_STATUS_ERROR_SECURE_NOT_SUPPORTED);
        return VCARD_DONE;
    }

    /* now parse the instruction */
    switch (apdu->a_ins) {
    case  VCARD7816_INS_MANAGE_CHANNEL: /* secure channel op */
    case  VCARD7816_INS_EXTERNAL_AUTHENTICATE: /* secure channel op */
    case  VCARD7816_INS_GET_CHALLENGE: /* secure channel op */
    case  VCARD7816_INS_INTERNAL_AUTHENTICATE: /* secure channel op */
    case  VCARD7816_INS_ERASE_BINARY: /* applet control op */
    case  VCARD7816_INS_READ_BINARY: /* applet control op */
    case  VCARD7816_INS_WRITE_BINARY: /* applet control op */
    case  VCARD7816_INS_UPDATE_BINARY: /* applet control op */
    case  VCARD7816_INS_READ_RECORD: /* file op */
    case  VCARD7816_INS_WRITE_RECORD: /* file op */
    case  VCARD7816_INS_UPDATE_RECORD: /* file op */
    case  VCARD7816_INS_APPEND_RECORD: /* file op */
    case  VCARD7816_INS_ENVELOPE:
    case  VCARD7816_INS_PUT_DATA:
        *response = vcard_make_response(
                            VCARD7816_STATUS_ERROR_COMMAND_NOT_SUPPORTED);
        break;

    case  VCARD7816_INS_SELECT_FILE:
        if (apdu->a_p1 != 0x04) {
            *response = vcard_make_response(
                            VCARD7816_STATUS_ERROR_FUNCTION_NOT_SUPPORTED);
            break;
        }

        /* side effect, deselect the current applet if no applet has been found
         * */
        current_applet = vcard_find_applet(card, apdu->a_body, apdu->a_Lc);
        vcard_select_applet(card, apdu->a_channel, current_applet);
        if (current_applet) {
            unsigned char *aid;
            int aid_len;
            aid = vcard_applet_get_aid(current_applet, &aid_len);
            *response = vcard_response_new(card, aid, aid_len, apdu->a_Le,
                                          VCARD7816_STATUS_SUCCESS);
        } else {
            *response = vcard_make_response(
                             VCARD7816_STATUS_ERROR_FILE_NOT_FOUND);
        }
        break;

    case  VCARD7816_INS_VERIFY:
        if ((apdu->a_p1 != 0x00) || (apdu->a_p2 != 0x00)) {
            *response = vcard_make_response(
                            VCARD7816_STATUS_ERROR_WRONG_PARAMETERS);
        } else {
            if (apdu->a_Lc == 0) {
                /* handle pin count if possible */
                count = vcard_emul_get_login_count(card);
                if (count < 0) {
                    *response = vcard_make_response(
                                    VCARD7816_STATUS_ERROR_DATA_NOT_FOUND);
                } else {
                    if (count > 0xf) {
                        count = 0xf;
                    }
                    *response = vcard_response_new_status_bytes(
                                                VCARD7816_SW1_WARNING_CHANGE,
                                                                0xc0 | count);
                    if (*response == NULL) {
                        *response = vcard_make_response(
                                    VCARD7816_STATUS_EXC_ERROR_MEMORY_FAILURE);
                    }
                }
            } else {
                    status = vcard_emul_login(card, apdu->a_body, apdu->a_Lc);
                *response = vcard_make_response(status);
            }
        }
        break;

    case VCARD7816_INS_GET_RESPONSE:
        buffer_response = vcard_get_buffer_response(card);
        if (!buffer_response) {
            *response = vcard_make_response(
                            VCARD7816_STATUS_ERROR_DATA_NOT_FOUND);
            /* handle error */
            break;
        }
        bytes_to_copy = MIN(buffer_response->len, apdu->a_Le);
        next_byte_count = MIN(256, buffer_response->len - bytes_to_copy);
        *response = vcard_response_new_bytes(
                        card, buffer_response->current, bytes_to_copy,
                        apdu->a_Le,
                        next_byte_count ?
                        VCARD7816_SW1_RESPONSE_BYTES : VCARD7816_SW1_SUCCESS,
                        next_byte_count);
        buffer_response->current += bytes_to_copy;
        buffer_response->len -= bytes_to_copy;
        if (*response == NULL || (next_byte_count == 0)) {
            vcard_set_buffer_response(card, NULL);
            vcard_buffer_response_delete(buffer_response);
        }
        if (*response == NULL) {
            *response =
                vcard_make_response(VCARD7816_STATUS_EXC_ERROR_MEMORY_FAILURE);
        }
        break;

    case VCARD7816_INS_GET_DATA:
        *response =
            vcard_make_response(VCARD7816_STATUS_ERROR_COMMAND_NOT_SUPPORTED);
        break;

    default:
        *response =
            vcard_make_response(VCARD7816_STATUS_ERROR_COMMAND_NOT_SUPPORTED);
        break;
    }

    /* response should have been set somewhere */
    assert(*response != NULL);
    return VCARD_DONE;
}


/*
 * APDU processing starts here. This routes the card processing stuff to the
 * right location.
 */
VCardStatus
vcard_process_apdu(VCard *card, VCardAPDU *apdu, VCardResponse **response)
{
    VCardStatus status;
    VCardBufferResponse *buffer_response;

    /* first handle any PTS commands, which aren't really APDU's */
    if (apdu->a_type == VCARD_7816_PTS) {
        /* the PTS responses aren't really responses either */
        *response = vcard_response_new_data(apdu->a_data, apdu->a_len);
        /* PTS responses have no status bytes */
        (*response)->b_total_len = (*response)->b_len;
        return VCARD_DONE;
    }
    buffer_response = vcard_get_buffer_response(card);
    if (buffer_response && apdu->a_ins != VCARD7816_INS_GET_RESPONSE) {
        /* clear out buffer_response, return an error */
        vcard_set_buffer_response(card, NULL);
        vcard_buffer_response_delete(buffer_response);
        *response = vcard_make_response(VCARD7816_STATUS_EXC_ERROR);
        return VCARD_DONE;
    }

    status = vcard_process_applet_apdu(card, apdu, response);
    if (status != VCARD_NEXT) {
        return status;
    }
    switch (vcard_get_type(card)) {
    case VCARD_FILE_SYSTEM:
        return vcard7816_file_system_process_apdu(card, apdu, response);
    case VCARD_VM:
        return vcard7816_vm_process_apdu(card, apdu, response);
    case VCARD_DIRECT:
        /* if we are type direct, then the applet should handle everything */
        assert("VCARD_DIRECT: applet failure");
        break;
    }
    *response =
        vcard_make_response(VCARD7816_STATUS_ERROR_COMMAND_NOT_SUPPORTED);
    return VCARD_DONE;
}
