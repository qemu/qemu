/*
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */
#ifndef VCARDT_H
#define VCARDT_H 1

/*
 * these should come from some common spice header file
 */
#include <assert.h>
#ifndef MIN
#define MIN(x, y) ((x) > (y) ? (y) : (x))
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#endif

typedef struct VCardStruct VCard;
typedef struct VCardAPDUStruct VCardAPDU;
typedef struct VCardResponseStruct VCardResponse;
typedef struct VCardBufferResponseStruct VCardBufferResponse;
typedef struct VCardAppletStruct VCardApplet;
typedef struct VCardAppletPrivateStruct VCardAppletPrivate;
typedef struct VCardKeyStruct VCardKey;  /* opaque */
typedef struct VCardEmulStruct VCardEmul;

#define MAX_CHANNEL 4

typedef enum {
    VCARD_DONE,
    VCARD_NEXT,
    VCARD_FAIL
} VCardStatus;

typedef enum {
    VCARD_FILE_SYSTEM,
    VCARD_VM,
    VCARD_DIRECT
} VCardType;

typedef enum {
    VCARD_POWER_ON,
    VCARD_POWER_OFF
} VCardPower;

typedef VCardStatus (*VCardProcessAPDU)(VCard *card, VCardAPDU *apdu,
                                        VCardResponse **response);
typedef VCardStatus (*VCardResetApplet)(VCard *card, int channel);
typedef void (*VCardAppletPrivateFree) (VCardAppletPrivate *);
typedef void (*VCardEmulFree) (VCardEmul *);
typedef void (*VCardGetAtr) (VCard *, unsigned char *atr, int *atr_len);

struct VCardBufferResponseStruct {
    unsigned char *buffer;
    int buffer_len;
    unsigned char *current;
    int len;
};

#endif
