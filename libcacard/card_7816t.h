/*
 * Implement the 7816 portion of the card spec
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */
#ifndef CARD_7816T_H
#define CARD_7816T_H 1

typedef unsigned short vcard_7816_status_t;

struct VCardResponseStruct {
    unsigned char *b_data;
    vcard_7816_status_t b_status;
    unsigned char b_sw1;
    unsigned char b_sw2;
    int b_len;
    int b_total_len;
    enum VCardResponseBufferType {
        VCARD_MALLOC,
        VCARD_MALLOC_DATA,
        VCARD_MALLOC_STRUCT,
        VCARD_STATIC
    } b_type;
};

#define VCARD_RESPONSE_NEW_STATIC_STATUS(stat) \
static const VCardResponse VCardResponse##stat = \
        {(unsigned char *)&VCardResponse##stat.b_sw1, (stat), ((stat) >> 8), \
         ((stat) & 0xff), 0, 2, VCARD_STATIC};

#define VCARD_RESPONSE_NEW_STATIC_STATUS_BYTES(sw1, sw2) \
static const VCardResponse VCARDResponse##sw1 = \
        {(unsigned char *)&VCardResponse##name.b_sw1, ((sw1) << 8 | (sw2)), \
         (sw1), (sw2), 0, 2, VCARD_STATIC};

/* cast away the const, callers need may need to 'free' the
 * result, and const implies that they don't */
#define VCARD_RESPONSE_GET_STATIC(name) \
        ((VCardResponse *)(&VCardResponse##name))

typedef enum {
    VCARD_7816_ISO,
    VCARD_7816_RFU,
    VCARD_7816_PTS,
    VCARD_7816_PROPIETARY
} VCardAPDUType;


/*
 * 7816 header. All APDU's have this header.
 * They must be laid out in this order.
 */
struct VCardAPDUHeader {
    unsigned char ah_cla;
    unsigned char ah_ins;
    unsigned char ah_p1;
    unsigned char ah_p2;
    unsigned char ah_Le;
    unsigned char ah_body[1]; /* indefinate length */
};

/*
 * 7816 APDU structure. The raw bytes are stored in the union and can be
 * accessed directly through u.data (which is aliased as a_data).
 *
 * Names of the fields match the 7816 documentation.
 */
struct VCardAPDUStruct {
    int a_len;                /* length of the whole buffer, including header */
    int a_Lc;                 /* 7816 Lc (parameter length) value */
    int a_Le;                 /* 7816 Le (expected result length) value */
    unsigned char *a_body;    /* pointer to the parameter */
    int a_channel;            /* decoded channel */
    int a_secure_messaging;   /* decoded secure messaging type */
    int a_type;               /* decoded type from cla (top nibble of class) */
    VCardAPDUType a_gen_type; /* generic type (7816, PROPRIETARY, RFU, etc) */
    union {
        struct VCardAPDUHeader *header;
        unsigned char   *data;
    } u;
/* give the subfields a unified look */
#define a_header u.header
#define a_data u.data
#define a_cla a_header->ah_cla /* class */
#define a_ins a_header->ah_ins /* instruction */
#define a_p1 a_header->ah_p1   /* parameter 1 */
#define a_p2 a_header->ah_p2   /* parameter 2 */
};

/* 7816 status codes */
#define VCARD7816_STATUS_SUCCESS                              0x9000
#define VCARD7816_STATUS_WARNING                              0x6200
#define VCARD7816_STATUS_WARNING_RET_CORUPT                   0x6281
#define VCARD7816_STATUS_WARNING_BUF_END_BEFORE_LE            0x6282
#define VCARD7816_STATUS_WARNING_INVALID_FILE_SELECTED        0x6283
#define VCARD7816_STATUS_WARNING_FCI_FORMAT_INVALID           0x6284
#define VCARD7816_STATUS_WARNING_CHANGE                       0x6300
#define VCARD7816_STATUS_WARNING_FILE_FILLED                  0x6381
#define VCARD7816_STATUS_EXC_ERROR                            0x6400
#define VCARD7816_STATUS_EXC_ERROR_CHANGE                     0x6500
#define VCARD7816_STATUS_EXC_ERROR_MEMORY_FAILURE             0x6581
#define VCARD7816_STATUS_ERROR_WRONG_LENGTH                   0x6700
#define VCARD7816_STATUS_ERROR_CLA_NOT_SUPPORTED              0x6800
#define VCARD7816_STATUS_ERROR_CHANNEL_NOT_SUPPORTED          0x6881
#define VCARD7816_STATUS_ERROR_SECURE_NOT_SUPPORTED           0x6882
#define VCARD7816_STATUS_ERROR_COMMAND_NOT_SUPPORTED          0x6900
#define VCARD7816_STATUS_ERROR_COMMAND_INCOMPATIBLE_WITH_FILE 0x6981
#define VCARD7816_STATUS_ERROR_SECURITY_NOT_SATISFIED         0x6982
#define VCARD7816_STATUS_ERROR_AUTHENTICATION_BLOCKED         0x6983
#define VCARD7816_STATUS_ERROR_DATA_INVALID                   0x6984
#define VCARD7816_STATUS_ERROR_CONDITION_NOT_SATISFIED        0x6985
#define VCARD7816_STATUS_ERROR_DATA_NO_EF                     0x6986
#define VCARD7816_STATUS_ERROR_SM_OBJECT_MISSING              0x6987
#define VCARD7816_STATUS_ERROR_SM_OBJECT_INCORRECT            0x6988
#define VCARD7816_STATUS_ERROR_WRONG_PARAMETERS               0x6a00
#define VCARD7816_STATUS_ERROR_WRONG_PARAMETERS_IN_DATA       0x6a80
#define VCARD7816_STATUS_ERROR_FUNCTION_NOT_SUPPORTED         0x6a81
#define VCARD7816_STATUS_ERROR_FILE_NOT_FOUND                 0x6a82
#define VCARD7816_STATUS_ERROR_RECORD_NOT_FOUND               0x6a83
#define VCARD7816_STATUS_ERROR_NO_SPACE_FOR_FILE              0x6a84
#define VCARD7816_STATUS_ERROR_LC_TLV_INCONSISTENT            0x6a85
#define VCARD7816_STATUS_ERROR_P1_P2_INCORRECT                0x6a86
#define VCARD7816_STATUS_ERROR_LC_P1_P2_INCONSISTENT          0x6a87
#define VCARD7816_STATUS_ERROR_DATA_NOT_FOUND                 0x6a88
#define VCARD7816_STATUS_ERROR_WRONG_PARAMETERS_2             0x6b00
#define VCARD7816_STATUS_ERROR_INS_CODE_INVALID               0x6d00
#define VCARD7816_STATUS_ERROR_CLA_INVALID                    0x6e00
#define VCARD7816_STATUS_ERROR_GENERAL                        0x6f00
/* 7816 sw1 codes */
#define VCARD7816_SW1_SUCCESS               0x90
#define VCARD7816_SW1_RESPONSE_BYTES        0x61
#define VCARD7816_SW1_WARNING               0x62
#define VCARD7816_SW1_WARNING_CHANGE        0x63
#define VCARD7816_SW1_EXC_ERROR             0x64
#define VCARD7816_SW1_EXC_ERROR_CHANGE      0x65
#define VCARD7816_SW1_ERROR_WRONG_LENGTH    0x67
#define VCARD7816_SW1_CLA_ERROR             0x68
#define VCARD7816_SW1_COMMAND_ERROR         0x69
#define VCARD7816_SW1_P1_P2_ERROR           0x6a
#define VCARD7816_SW1_LE_ERROR              0x6c
#define VCARD7816_SW1_INS_ERROR             0x6d
#define VCARD7816_SW1_CLA_NOT_SUPPORTED     0x6e

/* 7816 Instructions */
#define VCARD7816_INS_MANAGE_CHANNEL        0x70
#define VCARD7816_INS_EXTERNAL_AUTHENTICATE 0x82
#define VCARD7816_INS_GET_CHALLENGE         0x84
#define VCARD7816_INS_INTERNAL_AUTHENTICATE 0x88
#define VCARD7816_INS_ERASE_BINARY          0x0e
#define VCARD7816_INS_READ_BINARY           0xb0
#define VCARD7816_INS_WRITE_BINARY          0xd0
#define VCARD7816_INS_UPDATE_BINARY         0xd6
#define VCARD7816_INS_READ_RECORD           0xb2
#define VCARD7816_INS_WRITE_RECORD          0xd2
#define VCARD7816_INS_UPDATE_RECORD         0xdc
#define VCARD7816_INS_APPEND_RECORD         0xe2
#define VCARD7816_INS_ENVELOPE              0xc2
#define VCARD7816_INS_PUT_DATA              0xda
#define VCARD7816_INS_GET_DATA              0xca
#define VCARD7816_INS_SELECT_FILE           0xa4
#define VCARD7816_INS_VERIFY                0x20
#define VCARD7816_INS_GET_RESPONSE          0xc0

#endif
