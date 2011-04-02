/*
 * implement the Java card standard.
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include "qemu-common.h"

#include "vcard.h"
#include "vcard_emul.h"
#include "card_7816t.h"

struct VCardAppletStruct {
    VCardApplet   *next;
    VCardProcessAPDU process_apdu;
    VCardResetApplet reset_applet;
    unsigned char *aid;
    int aid_len;
    void *applet_private;
    VCardAppletPrivateFree applet_private_free;
};

struct VCardStruct {
    int reference_count;
    VCardApplet *applet_list;
    VCardApplet *current_applet[MAX_CHANNEL];
    VCardBufferResponse *vcard_buffer_response;
    VCardType type;
    VCardEmul *vcard_private;
    VCardEmulFree vcard_private_free;
    VCardGetAtr vcard_get_atr;
};

VCardBufferResponse *
vcard_buffer_response_new(unsigned char *buffer, int size)
{
    VCardBufferResponse *new_buffer;

    new_buffer = (VCardBufferResponse *)qemu_malloc(sizeof(VCardBufferResponse));
    new_buffer->buffer = (unsigned char *)qemu_malloc(size);
    memcpy(new_buffer->buffer, buffer, size);
    new_buffer->buffer_len = size;
    new_buffer->current = new_buffer->buffer;
    new_buffer->len = size;
    return new_buffer;
}

void
vcard_buffer_response_delete(VCardBufferResponse *buffer_response)
{
    if (buffer_response == NULL) {
        return;
    }
    if (buffer_response->buffer) {
        qemu_free(buffer_response->buffer);
    }
    qemu_free(buffer_response);
}


/*
 * clean up state after a reset
 */
void
vcard_reset(VCard *card, VCardPower power)
{
    int i;
    VCardApplet *applet = NULL;

    if (card->type ==  VCARD_DIRECT) {
        /* select the last applet */
        VCardApplet *current_applet = NULL;
        for (current_applet = card->applet_list; current_applet;
                                       current_applet = current_applet->next) {
            applet = current_applet;
        }
    }
    for (i = 0; i < MAX_CHANNEL; i++) {
        card->current_applet[i] = applet;
    }
    if (card->vcard_buffer_response) {
        vcard_buffer_response_delete(card->vcard_buffer_response);
        card->vcard_buffer_response = NULL;
    }
    vcard_emul_reset(card, power);
    if (applet) {
        applet->reset_applet(card, 0);
    }
}

/* applet utilities */

/*
 * applet utilities
 */
/* constructor */
VCardApplet *
vcard_new_applet(VCardProcessAPDU applet_process_function,
                 VCardResetApplet applet_reset_function,
                 unsigned char *aid, int aid_len)
{
    VCardApplet *applet;

    applet = (VCardApplet *)qemu_malloc(sizeof(VCardApplet));
    applet->next = NULL;
    applet->applet_private = NULL;
    applet->applet_private_free = NULL;
    applet->process_apdu = applet_process_function;
    applet->reset_applet = applet_reset_function;

    applet->aid = qemu_malloc(aid_len);
    memcpy(applet->aid, aid, aid_len);
    applet->aid_len = aid_len;
    return applet;
}

/* destructor */
void
vcard_delete_applet(VCardApplet *applet)
{
    if (applet == NULL) {
        return;
    }
    if (applet->applet_private_free) {
        applet->applet_private_free(applet->applet_private);
        applet->applet_private = NULL;
    }
    if (applet->aid) {
        qemu_free(applet->aid);
        applet->aid = NULL;
    }
    qemu_free(applet);
}

/* accessor */
void
vcard_set_applet_private(VCardApplet *applet, VCardAppletPrivate *private,
                         VCardAppletPrivateFree private_free)
{
    if (applet->applet_private_free) {
        applet->applet_private_free(applet->applet_private);
    }
    applet->applet_private = private;
    applet->applet_private_free = private_free;
}

VCard *
vcard_new(VCardEmul *private, VCardEmulFree private_free)
{
    VCard *new_card;
    int i;

    new_card = (VCard *)qemu_malloc(sizeof(VCard));
    new_card->applet_list = NULL;
    for (i = 0; i < MAX_CHANNEL; i++) {
        new_card->current_applet[i] = NULL;
    }
    new_card->vcard_buffer_response = NULL;
    new_card->type = VCARD_VM;
    new_card->vcard_private = private;
    new_card->vcard_private_free = private_free;
    new_card->vcard_get_atr = NULL;
    new_card->reference_count = 1;
    return new_card;
}

VCard *
vcard_reference(VCard *vcard)
{
    if (vcard == NULL) {
        return NULL;
    }
    vcard->reference_count++;
    return vcard;
}

void
vcard_free(VCard *vcard)
{
    VCardApplet *current_applet = NULL;
    VCardApplet *next_applet = NULL;

    if (vcard == NULL) {
        return;
    }
    vcard->reference_count--;
    if (vcard->reference_count != 0) {
        return;
    }
    if (vcard->vcard_private_free) {
        (*vcard->vcard_private_free)(vcard->vcard_private);
        vcard->vcard_private_free = 0;
        vcard->vcard_private = 0;
    }
    for (current_applet = vcard->applet_list; current_applet;
                                        current_applet = next_applet) {
        next_applet = current_applet->next;
        vcard_delete_applet(current_applet);
    }
    vcard_buffer_response_delete(vcard->vcard_buffer_response);
    qemu_free(vcard);
    return;
}

void
vcard_get_atr(VCard *vcard, unsigned char *atr, int *atr_len)
{
    if (vcard->vcard_get_atr) {
        (*vcard->vcard_get_atr)(vcard, atr, atr_len);
        return;
    }
    vcard_emul_get_atr(vcard, atr, atr_len);
}

void
vcard_set_atr_func(VCard *card, VCardGetAtr vcard_get_atr)
{
    card->vcard_get_atr = vcard_get_atr;
}


VCardStatus
vcard_add_applet(VCard *card, VCardApplet *applet)
{
    applet->next = card->applet_list;
    card->applet_list = applet;
    /* if our card-type is direct, always call the applet */
    if (card->type ==  VCARD_DIRECT) {
        int i;

        for (i = 0; i < MAX_CHANNEL; i++) {
            card->current_applet[i] = applet;
        }
    }
    return VCARD_DONE;
}

/*
 * manage applets
 */
VCardApplet *
vcard_find_applet(VCard *card, unsigned char *aid, int aid_len)
{
    VCardApplet *current_applet;

    for (current_applet = card->applet_list; current_applet;
                                        current_applet = current_applet->next) {
        if (current_applet->aid_len != aid_len) {
            continue;
        }
        if (memcmp(current_applet->aid, aid, aid_len) == 0) {
            break;
        }
    }
    return current_applet;
}

unsigned char *
vcard_applet_get_aid(VCardApplet *applet, int *aid_len)
{
    if (applet == NULL) {
        return NULL;
    }
    *aid_len = applet->aid_len;
    return applet->aid;
}


void
vcard_select_applet(VCard *card, int channel, VCardApplet *applet)
{
    assert(channel < MAX_CHANNEL);
    card->current_applet[channel] = applet;
    /* reset the applet */
    if (applet && applet->reset_applet) {
        applet->reset_applet(card, channel);
    }
}

VCardAppletPrivate *
vcard_get_current_applet_private(VCard *card, int channel)
{
    VCardApplet *applet = card->current_applet[channel];

    if (applet == NULL) {
        return NULL;
    }
    return applet->applet_private;
}

VCardStatus
vcard_process_applet_apdu(VCard *card, VCardAPDU *apdu,
                          VCardResponse **response)
{
    if (card->current_applet[apdu->a_channel]) {
        return card->current_applet[apdu->a_channel]->process_apdu(
                                                        card, apdu, response);
    }
    return VCARD_NEXT;
}

/*
 * Accessor functions
 */
/* accessor functions for the response buffer */
VCardBufferResponse *
vcard_get_buffer_response(VCard *card)
{
    return card->vcard_buffer_response;
}

void
vcard_set_buffer_response(VCard *card, VCardBufferResponse *buffer)
{
    card->vcard_buffer_response = buffer;
}


/* accessor functions for the type */
VCardType
vcard_get_type(VCard *card)
{
    return card->type;
}

void
vcard_set_type(VCard *card, VCardType type)
{
    card->type = type;
}

/* accessor for private data */
VCardEmul *
vcard_get_private(VCard *vcard)
{
    return vcard->vcard_private;
}

