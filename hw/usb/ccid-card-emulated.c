/*
 * CCID Card Device. Emulated card.
 *
 * Copyright (c) 2011 Red Hat.
 * Written by Alon Levy.
 *
 * This code is licensed under the GNU LGPL, version 2 or later.
 */

/*
 * It can be used to provide access to the local hardware in a non exclusive
 * way, or it can use certificates. It requires the usb-ccid bus.
 *
 * Usage 1: standard, mirror hardware reader+card:
 * qemu .. -usb -device usb-ccid -device ccid-card-emulated
 *
 * Usage 2: use certificates, no hardware required
 * one time: create the certificates:
 *  for i in 1 2 3; do
 *      certutil -d /etc/pki/nssdb -x -t "CT,CT,CT" -S -s "CN=user$i" -n user$i
 *  done
 * qemu .. -usb -device usb-ccid \
 *  -device ccid-card-emulated,cert1=user1,cert2=user2,cert3=user3
 *
 * If you use a non default db for the certificates you can specify it using
 * the db parameter.
 */

#include "qemu/osdep.h"
#include <libcacard.h>

#include "qemu/thread.h"
#include "qemu/lockable.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "ccid.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "qom/object.h"

#define DPRINTF(card, lvl, fmt, ...) \
do {\
    if (lvl <= card->debug) {\
        printf("ccid-card-emul: %s: " fmt , __func__, ## __VA_ARGS__);\
    } \
} while (0)


#define TYPE_EMULATED_CCID "ccid-card-emulated"
typedef struct EmulatedState EmulatedState;
DECLARE_INSTANCE_CHECKER(EmulatedState, EMULATED_CCID_CARD,
                         TYPE_EMULATED_CCID)

#define BACKEND_NSS_EMULATED_NAME "nss-emulated"
#define BACKEND_CERTIFICATES_NAME "certificates"

enum {
    BACKEND_NSS_EMULATED = 1,
    BACKEND_CERTIFICATES
};

#define DEFAULT_BACKEND BACKEND_NSS_EMULATED


enum {
    EMUL_READER_INSERT = 0,
    EMUL_READER_REMOVE,
    EMUL_CARD_INSERT,
    EMUL_CARD_REMOVE,
    EMUL_GUEST_APDU,
    EMUL_RESPONSE_APDU,
    EMUL_ERROR,
};

static const char *emul_event_to_string(uint32_t emul_event)
{
    switch (emul_event) {
    case EMUL_READER_INSERT:
        return "EMUL_READER_INSERT";
    case EMUL_READER_REMOVE:
        return "EMUL_READER_REMOVE";
    case EMUL_CARD_INSERT:
        return "EMUL_CARD_INSERT";
    case EMUL_CARD_REMOVE:
        return "EMUL_CARD_REMOVE";
    case EMUL_GUEST_APDU:
        return "EMUL_GUEST_APDU";
    case EMUL_RESPONSE_APDU:
        return "EMUL_RESPONSE_APDU";
    case EMUL_ERROR:
        return "EMUL_ERROR";
    }
    return "UNKNOWN";
}

typedef struct EmulEvent {
    QSIMPLEQ_ENTRY(EmulEvent) entry;
    union {
        struct {
            uint32_t type;
        } gen;
        struct {
            uint32_t type;
            uint64_t code;
        } error;
        struct {
            uint32_t type;
            uint32_t len;
            uint8_t data[];
        } data;
    } p;
} EmulEvent;

#define MAX_ATR_SIZE 40
struct EmulatedState {
    CCIDCardState base;
    uint8_t  debug;
    char    *backend_str;
    uint32_t backend;
    char    *cert1;
    char    *cert2;
    char    *cert3;
    char    *db;
    uint8_t  atr[MAX_ATR_SIZE];
    uint8_t  atr_length;
    QSIMPLEQ_HEAD(, EmulEvent) event_list;
    QemuMutex event_list_mutex;
    QemuThread event_thread_id;
    VReader *reader;
    QSIMPLEQ_HEAD(, EmulEvent) guest_apdu_list;
    QemuMutex vreader_mutex; /* and guest_apdu_list mutex */
    QemuMutex handle_apdu_mutex;
    QemuCond handle_apdu_cond;
    EventNotifier notifier;
    int      quit_apdu_thread;
    QemuThread apdu_thread_id;
};

static void emulated_apdu_from_guest(CCIDCardState *base,
    const uint8_t *apdu, uint32_t len)
{
    EmulatedState *card = EMULATED_CCID_CARD(base);
    EmulEvent *event = g_malloc(sizeof(EmulEvent) + len);

    assert(event);
    event->p.data.type = EMUL_GUEST_APDU;
    event->p.data.len = len;
    memcpy(event->p.data.data, apdu, len);
    qemu_mutex_lock(&card->vreader_mutex);
    QSIMPLEQ_INSERT_TAIL(&card->guest_apdu_list, event, entry);
    qemu_mutex_unlock(&card->vreader_mutex);
    qemu_mutex_lock(&card->handle_apdu_mutex);
    qemu_cond_signal(&card->handle_apdu_cond);
    qemu_mutex_unlock(&card->handle_apdu_mutex);
}

static const uint8_t *emulated_get_atr(CCIDCardState *base, uint32_t *len)
{
    EmulatedState *card = EMULATED_CCID_CARD(base);

    *len = card->atr_length;
    return card->atr;
}

static void emulated_push_event(EmulatedState *card, EmulEvent *event)
{
    qemu_mutex_lock(&card->event_list_mutex);
    QSIMPLEQ_INSERT_TAIL(&(card->event_list), event, entry);
    qemu_mutex_unlock(&card->event_list_mutex);
    event_notifier_set(&card->notifier);
}

static void emulated_push_type(EmulatedState *card, uint32_t type)
{
    EmulEvent *event = g_new(EmulEvent, 1);

    assert(event);
    event->p.gen.type = type;
    emulated_push_event(card, event);
}

static void emulated_push_error(EmulatedState *card, uint64_t code)
{
    EmulEvent *event = g_new(EmulEvent, 1);

    assert(event);
    event->p.error.type = EMUL_ERROR;
    event->p.error.code = code;
    emulated_push_event(card, event);
}

static void emulated_push_data_type(EmulatedState *card, uint32_t type,
    const uint8_t *data, uint32_t len)
{
    EmulEvent *event = (EmulEvent *)g_malloc(sizeof(EmulEvent) + len);

    assert(event);
    event->p.data.type = type;
    event->p.data.len = len;
    memcpy(event->p.data.data, data, len);
    emulated_push_event(card, event);
}

static void emulated_push_reader_insert(EmulatedState *card)
{
    emulated_push_type(card, EMUL_READER_INSERT);
}

static void emulated_push_reader_remove(EmulatedState *card)
{
    emulated_push_type(card, EMUL_READER_REMOVE);
}

static void emulated_push_card_insert(EmulatedState *card,
    const uint8_t *atr, uint32_t len)
{
    emulated_push_data_type(card, EMUL_CARD_INSERT, atr, len);
}

static void emulated_push_card_remove(EmulatedState *card)
{
    emulated_push_type(card, EMUL_CARD_REMOVE);
}

static void emulated_push_response_apdu(EmulatedState *card,
    const uint8_t *apdu, uint32_t len)
{
    emulated_push_data_type(card, EMUL_RESPONSE_APDU, apdu, len);
}

#define APDU_BUF_SIZE 270
static void *handle_apdu_thread(void* arg)
{
    EmulatedState *card = arg;
    uint8_t recv_data[APDU_BUF_SIZE];
    int recv_len;
    VReaderStatus reader_status;
    EmulEvent *event;

    while (1) {
        qemu_mutex_lock(&card->handle_apdu_mutex);
        qemu_cond_wait(&card->handle_apdu_cond, &card->handle_apdu_mutex);
        qemu_mutex_unlock(&card->handle_apdu_mutex);
        if (card->quit_apdu_thread) {
            card->quit_apdu_thread = 0; /* debugging */
            break;
        }
        WITH_QEMU_LOCK_GUARD(&card->vreader_mutex) {
            while (!QSIMPLEQ_EMPTY(&card->guest_apdu_list)) {
                event = QSIMPLEQ_FIRST(&card->guest_apdu_list);
                assert(event != NULL);
                QSIMPLEQ_REMOVE_HEAD(&card->guest_apdu_list, entry);
                if (event->p.data.type != EMUL_GUEST_APDU) {
                    DPRINTF(card, 1, "unexpected message in handle_apdu_thread\n");
                    g_free(event);
                    continue;
                }
                if (card->reader == NULL) {
                    DPRINTF(card, 1, "reader is NULL\n");
                    g_free(event);
                    continue;
                }
                recv_len = sizeof(recv_data);
                reader_status = vreader_xfr_bytes(card->reader,
                        event->p.data.data, event->p.data.len,
                        recv_data, &recv_len);
                DPRINTF(card, 2, "got back apdu of length %d\n", recv_len);
                if (reader_status == VREADER_OK) {
                    emulated_push_response_apdu(card, recv_data, recv_len);
                } else {
                    emulated_push_error(card, reader_status);
                }
                g_free(event);
            }
        }
    }
    return NULL;
}

static void *event_thread(void *arg)
{
    int atr_len = MAX_ATR_SIZE;
    uint8_t atr[MAX_ATR_SIZE];
    VEvent *event = NULL;
    EmulatedState *card = arg;

    while (1) {
        const char *reader_name;

        event = vevent_wait_next_vevent();
        if (event == NULL || event->type == VEVENT_LAST) {
            break;
        }
        if (event->type != VEVENT_READER_INSERT) {
            if (card->reader == NULL && event->reader != NULL) {
                /* Happens after device_add followed by card remove or insert.
                 * XXX: create synthetic add_reader events if vcard_emul_init
                 * already called, which happens if device_del and device_add
                 * are called */
                card->reader = vreader_reference(event->reader);
            } else {
                if (event->reader != card->reader) {
                    fprintf(stderr,
                        "ERROR: wrong reader: quitting event_thread\n");
                    break;
                }
            }
        }
        switch (event->type) {
        case VEVENT_READER_INSERT:
            /* TODO: take a specific reader. i.e. track which reader
             * we are seeing here, check it is the one we want (the first,
             * or by a particular name), and ignore if we don't want it.
             */
            reader_name = vreader_get_name(event->reader);
            if (card->reader != NULL) {
                DPRINTF(card, 2, "READER INSERT - replacing %s with %s\n",
                    vreader_get_name(card->reader), reader_name);
                qemu_mutex_lock(&card->vreader_mutex);
                vreader_free(card->reader);
                qemu_mutex_unlock(&card->vreader_mutex);
                emulated_push_reader_remove(card);
            }
            qemu_mutex_lock(&card->vreader_mutex);
            DPRINTF(card, 2, "READER INSERT %s\n", reader_name);
            card->reader = vreader_reference(event->reader);
            qemu_mutex_unlock(&card->vreader_mutex);
            emulated_push_reader_insert(card);
            break;
        case VEVENT_READER_REMOVE:
            DPRINTF(card, 2, " READER REMOVE: %s\n",
                    vreader_get_name(event->reader));
            qemu_mutex_lock(&card->vreader_mutex);
            vreader_free(card->reader);
            card->reader = NULL;
            qemu_mutex_unlock(&card->vreader_mutex);
            emulated_push_reader_remove(card);
            break;
        case VEVENT_CARD_INSERT:
            /* get the ATR (intended as a response to a power on from the
             * reader */
            atr_len = MAX_ATR_SIZE;
            vreader_power_on(event->reader, atr, &atr_len);
            card->atr_length = (uint8_t)atr_len;
            DPRINTF(card, 2, " CARD INSERT\n");
            emulated_push_card_insert(card, atr, atr_len);
            break;
        case VEVENT_CARD_REMOVE:
            DPRINTF(card, 2, " CARD REMOVE\n");
            emulated_push_card_remove(card);
            break;
        case VEVENT_LAST: /* quit */
            vevent_delete(event);
            return NULL;
        default:
            break;
        }
        vevent_delete(event);
    }
    return NULL;
}

static void card_event_handler(EventNotifier *notifier)
{
    EmulatedState *card = container_of(notifier, EmulatedState, notifier);
    EmulEvent *event, *next;

    event_notifier_test_and_clear(&card->notifier);
    QEMU_LOCK_GUARD(&card->event_list_mutex);
    QSIMPLEQ_FOREACH_SAFE(event, &card->event_list, entry, next) {
        DPRINTF(card, 2, "event %s\n", emul_event_to_string(event->p.gen.type));
        switch (event->p.gen.type) {
        case EMUL_RESPONSE_APDU:
            ccid_card_send_apdu_to_guest(&card->base, event->p.data.data,
                event->p.data.len);
            break;
        case EMUL_READER_INSERT:
            ccid_card_ccid_attach(&card->base);
            break;
        case EMUL_READER_REMOVE:
            ccid_card_ccid_detach(&card->base);
            break;
        case EMUL_CARD_INSERT:
            assert(event->p.data.len <= MAX_ATR_SIZE);
            card->atr_length = event->p.data.len;
            memcpy(card->atr, event->p.data.data, card->atr_length);
            ccid_card_card_inserted(&card->base);
            break;
        case EMUL_CARD_REMOVE:
            ccid_card_card_removed(&card->base);
            break;
        case EMUL_ERROR:
            ccid_card_card_error(&card->base, event->p.error.code);
            break;
        default:
            DPRINTF(card, 2, "unexpected event\n");
            break;
        }
        g_free(event);
    }
    QSIMPLEQ_INIT(&card->event_list);
}

static int init_event_notifier(EmulatedState *card, Error **errp)
{
    if (event_notifier_init(&card->notifier, false) < 0) {
        error_setg(errp, "ccid-card-emul: event notifier creation failed");
        return -1;
    }
    event_notifier_set_handler(&card->notifier, card_event_handler);
    return 0;
}

static void clean_event_notifier(EmulatedState *card)
{
    event_notifier_set_handler(&card->notifier, NULL);
    event_notifier_cleanup(&card->notifier);
}

#define CERTIFICATES_DEFAULT_DB "/etc/pki/nssdb"
#define CERTIFICATES_ARGS_TEMPLATE\
    "db=\"%s\" use_hw=no soft=(,Virtual Reader,CAC,,%s,%s,%s)"

static int wrap_vcard_emul_init(VCardEmulOptions *options)
{
    static int called;
    static int options_was_null;

    if (called) {
        if ((options == NULL) != options_was_null) {
            printf("%s: warning: running emulated with certificates"
                   " and emulated side by side is not supported\n",
                   __func__);
            return VCARD_EMUL_FAIL;
        }
        vcard_emul_replay_insertion_events();
        return VCARD_EMUL_OK;
    }
    options_was_null = (options == NULL);
    called = 1;
    return vcard_emul_init(options);
}

static int emulated_initialize_vcard_from_certificates(EmulatedState *card)
{
    char emul_args[200];
    VCardEmulOptions *options = NULL;

    snprintf(emul_args, sizeof(emul_args) - 1, CERTIFICATES_ARGS_TEMPLATE,
        card->db ? card->db : CERTIFICATES_DEFAULT_DB,
        card->cert1, card->cert2, card->cert3);
    options = vcard_emul_options(emul_args);
    if (options == NULL) {
        printf("%s: warning: not using certificates due to"
               " initialization error\n", __func__);
    }
    return wrap_vcard_emul_init(options);
}

typedef struct EnumTable {
    const char *name;
    uint32_t value;
} EnumTable;

static const EnumTable backend_enum_table[] = {
    {BACKEND_NSS_EMULATED_NAME, BACKEND_NSS_EMULATED},
    {BACKEND_CERTIFICATES_NAME, BACKEND_CERTIFICATES},
    {NULL, 0},
};

static uint32_t parse_enumeration(char *str,
    const EnumTable *table, uint32_t not_found_value)
{
    uint32_t ret = not_found_value;

    if (str == NULL)
        return 0;

    while (table->name != NULL) {
        if (strcmp(table->name, str) == 0) {
            ret = table->value;
            break;
        }
        table++;
    }
    return ret;
}

static void emulated_realize(CCIDCardState *base, Error **errp)
{
    EmulatedState *card = EMULATED_CCID_CARD(base);
    VCardEmulError ret;
    const EnumTable *ptable;

    QSIMPLEQ_INIT(&card->event_list);
    QSIMPLEQ_INIT(&card->guest_apdu_list);
    qemu_mutex_init(&card->event_list_mutex);
    qemu_mutex_init(&card->vreader_mutex);
    qemu_mutex_init(&card->handle_apdu_mutex);
    qemu_cond_init(&card->handle_apdu_cond);
    card->reader = NULL;
    card->quit_apdu_thread = 0;
    if (init_event_notifier(card, errp) < 0) {
        goto out1;
    }

    card->backend = 0;
    if (card->backend_str) {
        card->backend = parse_enumeration(card->backend_str,
                                          backend_enum_table, 0);
    }

    if (card->backend == 0) {
        error_setg(errp, "backend must be one of:");
        for (ptable = backend_enum_table; ptable->name != NULL; ++ptable) {
            error_append_hint(errp, "%s\n", ptable->name);
        }
        goto out2;
    }

    /* TODO: a passthru backend that works on local machine. third card type?*/
    if (card->backend == BACKEND_CERTIFICATES) {
        if (card->cert1 != NULL && card->cert2 != NULL && card->cert3 != NULL) {
            ret = emulated_initialize_vcard_from_certificates(card);
        } else {
            error_setg(errp, "%s: you must provide all three certs for"
                       " certificates backend", TYPE_EMULATED_CCID);
            goto out2;
        }
    } else {
        if (card->backend != BACKEND_NSS_EMULATED) {
            error_setg(errp, "%s: bad backend specified. The options are:%s"
                       " (default), %s.", TYPE_EMULATED_CCID,
                       BACKEND_NSS_EMULATED_NAME, BACKEND_CERTIFICATES_NAME);
            goto out2;
        }
        if (card->cert1 != NULL || card->cert2 != NULL || card->cert3 != NULL) {
            error_setg(errp, "%s: unexpected cert parameters to nss emulated "
                       "backend", TYPE_EMULATED_CCID);
            goto out2;
        }
        /* default to mirroring the local hardware readers */
        ret = wrap_vcard_emul_init(NULL);
    }
    if (ret != VCARD_EMUL_OK) {
        error_setg(errp, "%s: failed to initialize vcard", TYPE_EMULATED_CCID);
        goto out2;
    }
    qemu_thread_create(&card->event_thread_id, "ccid/event", event_thread,
                       card, QEMU_THREAD_JOINABLE);
    qemu_thread_create(&card->apdu_thread_id, "ccid/apdu", handle_apdu_thread,
                       card, QEMU_THREAD_JOINABLE);

    return;

out2:
    clean_event_notifier(card);
out1:
    qemu_cond_destroy(&card->handle_apdu_cond);
    qemu_mutex_destroy(&card->handle_apdu_mutex);
    qemu_mutex_destroy(&card->vreader_mutex);
    qemu_mutex_destroy(&card->event_list_mutex);
}

static void emulated_unrealize(CCIDCardState *base)
{
    EmulatedState *card = EMULATED_CCID_CARD(base);
    VEvent *vevent = vevent_new(VEVENT_LAST, NULL, NULL);

    vevent_queue_vevent(vevent); /* stop vevent thread */
    qemu_thread_join(&card->event_thread_id);

    card->quit_apdu_thread = 1; /* stop handle_apdu thread */
    qemu_cond_signal(&card->handle_apdu_cond);
    qemu_thread_join(&card->apdu_thread_id);

    clean_event_notifier(card);
    /* threads exited, can destroy all condvars/mutexes */
    qemu_cond_destroy(&card->handle_apdu_cond);
    qemu_mutex_destroy(&card->handle_apdu_mutex);
    qemu_mutex_destroy(&card->vreader_mutex);
    qemu_mutex_destroy(&card->event_list_mutex);
}

static const Property emulated_card_properties[] = {
    DEFINE_PROP_STRING("backend", EmulatedState, backend_str),
    DEFINE_PROP_STRING("cert1", EmulatedState, cert1),
    DEFINE_PROP_STRING("cert2", EmulatedState, cert2),
    DEFINE_PROP_STRING("cert3", EmulatedState, cert3),
    DEFINE_PROP_STRING("db", EmulatedState, db),
    DEFINE_PROP_UINT8("debug", EmulatedState, debug, 0),
};

static void emulated_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    CCIDCardClass *cc = CCID_CARD_CLASS(klass);

    cc->realize = emulated_realize;
    cc->unrealize = emulated_unrealize;
    cc->get_atr = emulated_get_atr;
    cc->apdu_from_guest = emulated_apdu_from_guest;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
    dc->desc = "emulated smartcard";
    device_class_set_props(dc, emulated_card_properties);
}

static const TypeInfo emulated_card_info = {
    .name          = TYPE_EMULATED_CCID,
    .parent        = TYPE_CCID_CARD,
    .instance_size = sizeof(EmulatedState),
    .class_init    = emulated_class_initfn,
};
module_obj(TYPE_EMULATED_CCID);
module_kconfig(USB);

static void ccid_card_emulated_register_types(void)
{
    type_register_static(&emulated_card_info);
}

type_init(ccid_card_emulated_register_types)
