/*
 * Tester for VSCARD protocol, client side.
 *
 * Can be used with ccid-card-passthru.
 *
 * Copyright (c) 2011 Red Hat.
 * Written by Alon Levy.
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#ifndef _WIN32
#include <netdb.h>
#endif
#include <glib.h>

#include "qemu-common.h"
#include "qemu/thread.h"
#include "qemu/sockets.h"

#include "vscard_common.h"

#include "vreader.h"
#include "vcard_emul.h"
#include "vevent.h"

static int verbose;

static void
print_byte_array(
    uint8_t *arrBytes,
    unsigned int nSize
) {
    int i;
    for (i = 0; i < nSize; i++) {
        printf("%02X ", arrBytes[i]);
    }
    printf("\n");
}

static void
print_usage(void) {
    printf("vscclient [-c <certname> .. -e <emul_args> -d <level>%s] "
            "<host> <port>\n",
#ifdef USE_PASSTHRU
    " -p");
    printf(" -p use passthrough mode\n");
#else
   "");
#endif
    vcard_emul_usage();
}

static GIOChannel *channel_socket;
static GByteArray *socket_to_send;
static QemuMutex socket_to_send_lock;
static guint socket_tag;

static void
update_socket_watch(void);

static gboolean
do_socket_send(GIOChannel *source,
               GIOCondition condition,
               gpointer data)
{
    gsize bw;
    GError *err = NULL;

    g_return_val_if_fail(socket_to_send->len != 0, FALSE);
    g_return_val_if_fail(condition & G_IO_OUT, FALSE);

    g_io_channel_write_chars(channel_socket,
        (gchar *)socket_to_send->data, socket_to_send->len, &bw, &err);
    if (err != NULL) {
        g_error("Error while sending socket %s", err->message);
        return FALSE;
    }
    g_byte_array_remove_range(socket_to_send, 0, bw);

    if (socket_to_send->len == 0) {
        update_socket_watch();
        return FALSE;
    }
    return TRUE;
}

static gboolean
socket_prepare_sending(gpointer user_data)
{
    update_socket_watch();

    return FALSE;
}

static int
send_msg(
    VSCMsgType type,
    uint32_t reader_id,
    const void *msg,
    unsigned int length
) {
    VSCMsgHeader mhHeader;

    qemu_mutex_lock(&socket_to_send_lock);

    if (verbose > 10) {
        printf("sending type=%d id=%u, len =%u (0x%x)\n",
               type, reader_id, length, length);
    }

    mhHeader.type = htonl(type);
    mhHeader.reader_id = 0;
    mhHeader.length = htonl(length);
    g_byte_array_append(socket_to_send, (guint8 *)&mhHeader, sizeof(mhHeader));
    g_byte_array_append(socket_to_send, (guint8 *)msg, length);
    g_idle_add(socket_prepare_sending, NULL);

    qemu_mutex_unlock(&socket_to_send_lock);

    return 0;
}

static VReader *pending_reader;
static QemuMutex pending_reader_lock;
static QemuCond pending_reader_condition;

#define MAX_ATR_LEN 40
static void *
event_thread(void *arg)
{
    unsigned char atr[MAX_ATR_LEN];
    int atr_len = MAX_ATR_LEN;
    VEvent *event = NULL;
    unsigned int reader_id;


    while (1) {
        const char *reader_name;

        event = vevent_wait_next_vevent();
        if (event == NULL) {
            break;
        }
        reader_id = vreader_get_id(event->reader);
        if (reader_id == VSCARD_UNDEFINED_READER_ID &&
            event->type != VEVENT_READER_INSERT) {
            /* ignore events from readers qemu has rejected */
            /* if qemu is still deciding on this reader, wait to see if need to
             * forward this event */
            qemu_mutex_lock(&pending_reader_lock);
            if (!pending_reader || (pending_reader != event->reader)) {
                /* wasn't for a pending reader, this reader has already been
                 * rejected by qemu */
                qemu_mutex_unlock(&pending_reader_lock);
                vevent_delete(event);
                continue;
            }
            /* this reader hasn't been told its status from qemu yet, wait for
             * that status */
            while (pending_reader != NULL) {
                qemu_cond_wait(&pending_reader_condition, &pending_reader_lock);
            }
            qemu_mutex_unlock(&pending_reader_lock);
            /* now recheck the id */
            reader_id = vreader_get_id(event->reader);
            if (reader_id == VSCARD_UNDEFINED_READER_ID) {
                /* this reader was rejected */
                vevent_delete(event);
                continue;
            }
            /* reader was accepted, now forward the event */
        }
        switch (event->type) {
        case VEVENT_READER_INSERT:
            /* tell qemu to insert a new CCID reader */
            /* wait until qemu has responded to our first reader insert
             * before we send a second. That way we won't confuse the responses
             * */
            qemu_mutex_lock(&pending_reader_lock);
            while (pending_reader != NULL) {
                qemu_cond_wait(&pending_reader_condition, &pending_reader_lock);
            }
            pending_reader = vreader_reference(event->reader);
            qemu_mutex_unlock(&pending_reader_lock);
            reader_name = vreader_get_name(event->reader);
            if (verbose > 10) {
                printf(" READER INSERT: %s\n", reader_name);
            }
            send_msg(VSC_ReaderAdd,
                reader_id, /* currerntly VSCARD_UNDEFINED_READER_ID */
                NULL, 0 /* TODO reader_name, strlen(reader_name) */);
            break;
        case VEVENT_READER_REMOVE:
            /* future, tell qemu that an old CCID reader has been removed */
            if (verbose > 10) {
                printf(" READER REMOVE: %u\n", reader_id);
            }
            send_msg(VSC_ReaderRemove, reader_id, NULL, 0);
            break;
        case VEVENT_CARD_INSERT:
            /* get the ATR (intended as a response to a power on from the
             * reader */
            atr_len = MAX_ATR_LEN;
            vreader_power_on(event->reader, atr, &atr_len);
            /* ATR call functions as a Card Insert event */
            if (verbose > 10) {
                printf(" CARD INSERT %u: ", reader_id);
                print_byte_array(atr, atr_len);
            }
            send_msg(VSC_ATR, reader_id, atr, atr_len);
            break;
        case VEVENT_CARD_REMOVE:
            /* Card removed */
            if (verbose > 10) {
                printf(" CARD REMOVE %u:\n", reader_id);
            }
            send_msg(VSC_CardRemove, reader_id, NULL, 0);
            break;
        default:
            break;
        }
        vevent_delete(event);
    }
    return NULL;
}


static unsigned int
get_id_from_string(char *string, unsigned int default_id)
{
    unsigned int id = atoi(string);

    /* don't accidentally swith to zero because no numbers have been supplied */
    if ((id == 0) && *string != '0') {
        return default_id;
    }
    return id;
}

static int
on_host_init(VSCMsgHeader *mhHeader, VSCMsgInit *incoming)
{
    uint32_t *capabilities = (incoming->capabilities);
    int num_capabilities =
        1 + ((mhHeader->length - sizeof(VSCMsgInit)) / sizeof(uint32_t));
    int i;
    QemuThread thread_id;

    incoming->version = ntohl(incoming->version);
    if (incoming->version != VSCARD_VERSION) {
        if (verbose > 0) {
            printf("warning: host has version %d, we have %d\n",
                verbose, VSCARD_VERSION);
        }
    }
    if (incoming->magic != VSCARD_MAGIC) {
        printf("unexpected magic: got %d, expected %d\n",
            incoming->magic, VSCARD_MAGIC);
        return -1;
    }
    for (i = 0 ; i < num_capabilities; ++i) {
        capabilities[i] = ntohl(capabilities[i]);
    }
    /* Future: check capabilities */
    /* remove whatever reader might be left in qemu,
     * in case of an unclean previous exit. */
    send_msg(VSC_ReaderRemove, VSCARD_MINIMAL_READER_ID, NULL, 0);
    /* launch the event_thread. This will trigger reader adds for all the
     * existing readers */
    qemu_thread_create(&thread_id, "vsc/event", event_thread, NULL, 0);
    return 0;
}


enum {
    STATE_HEADER,
    STATE_MESSAGE,
};

#define APDUBufSize 270

static gboolean
do_socket_read(GIOChannel *source,
               GIOCondition condition,
               gpointer data)
{
    int rv;
    int dwSendLength;
    int dwRecvLength;
    uint8_t pbRecvBuffer[APDUBufSize];
    static uint8_t pbSendBuffer[APDUBufSize];
    VReaderStatus reader_status;
    VReader *reader = NULL;
    static VSCMsgHeader mhHeader;
    VSCMsgError *error_msg;
    GError *err = NULL;

    static gchar *buf;
    static gsize br, to_read;
    static int state = STATE_HEADER;

    if (state == STATE_HEADER && to_read == 0) {
        buf = (gchar *)&mhHeader;
        to_read = sizeof(mhHeader);
    }

    if (to_read > 0) {
        g_io_channel_read_chars(source, (gchar *)buf, to_read, &br, &err);
        if (err != NULL) {
            g_error("error while reading: %s", err->message);
        }
        buf += br;
        to_read -= br;
        if (to_read != 0) {
            return TRUE;
        }
    }

    if (state == STATE_HEADER) {
        mhHeader.type = ntohl(mhHeader.type);
        mhHeader.reader_id = ntohl(mhHeader.reader_id);
        mhHeader.length = ntohl(mhHeader.length);
        if (verbose) {
            printf("Header: type=%d, reader_id=%u length=%d (0x%x)\n",
                   mhHeader.type, mhHeader.reader_id, mhHeader.length,
                   mhHeader.length);
        }
        switch (mhHeader.type) {
        case VSC_APDU:
        case VSC_Flush:
        case VSC_Error:
        case VSC_Init:
            buf = (gchar *)pbSendBuffer;
            to_read = mhHeader.length;
            state = STATE_MESSAGE;
            return TRUE;
        default:
            fprintf(stderr, "Unexpected message of type 0x%X\n", mhHeader.type);
            return FALSE;
        }
    }

    if (state == STATE_MESSAGE) {
        switch (mhHeader.type) {
        case VSC_APDU:
            if (verbose) {
                printf(" recv APDU: ");
                print_byte_array(pbSendBuffer, mhHeader.length);
            }
            /* Transmit received APDU */
            dwSendLength = mhHeader.length;
            dwRecvLength = sizeof(pbRecvBuffer);
            reader = vreader_get_reader_by_id(mhHeader.reader_id);
            reader_status = vreader_xfr_bytes(reader,
                                              pbSendBuffer, dwSendLength,
                                              pbRecvBuffer, &dwRecvLength);
            if (reader_status == VREADER_OK) {
                mhHeader.length = dwRecvLength;
                if (verbose) {
                    printf(" send response: ");
                    print_byte_array(pbRecvBuffer, mhHeader.length);
                }
                send_msg(VSC_APDU, mhHeader.reader_id,
                         pbRecvBuffer, dwRecvLength);
            } else {
                rv = reader_status; /* warning: not meaningful */
                send_msg(VSC_Error, mhHeader.reader_id, &rv, sizeof(uint32_t));
            }
            vreader_free(reader);
            reader = NULL; /* we've freed it, don't use it by accident
                              again */
            break;
        case VSC_Flush:
            /* TODO: actually flush */
            send_msg(VSC_FlushComplete, mhHeader.reader_id, NULL, 0);
            break;
        case VSC_Error:
            error_msg = (VSCMsgError *) pbSendBuffer;
            if (error_msg->code == VSC_SUCCESS) {
                qemu_mutex_lock(&pending_reader_lock);
                if (pending_reader) {
                    vreader_set_id(pending_reader, mhHeader.reader_id);
                    vreader_free(pending_reader);
                    pending_reader = NULL;
                    qemu_cond_signal(&pending_reader_condition);
                }
                qemu_mutex_unlock(&pending_reader_lock);
                break;
            }
            printf("warning: qemu refused to add reader\n");
            if (error_msg->code == VSC_CANNOT_ADD_MORE_READERS) {
                /* clear pending reader, qemu can't handle any more */
                qemu_mutex_lock(&pending_reader_lock);
                if (pending_reader) {
                    pending_reader = NULL;
                    /* make sure the event loop doesn't hang */
                    qemu_cond_signal(&pending_reader_condition);
                }
                qemu_mutex_unlock(&pending_reader_lock);
            }
            break;
        case VSC_Init:
            if (on_host_init(&mhHeader, (VSCMsgInit *)pbSendBuffer) < 0) {
                return FALSE;
            }
            break;
        default:
            g_assert_not_reached();
            return FALSE;
        }

        state = STATE_HEADER;
    }


    return TRUE;
}

static gboolean
do_socket(GIOChannel *source,
          GIOCondition condition,
          gpointer data)
{
    /* not sure if two watches work well with a single win32 sources */
    if (condition & G_IO_OUT) {
        if (!do_socket_send(source, condition, data)) {
            return FALSE;
        }
    }

    if (condition & G_IO_IN) {
        if (!do_socket_read(source, condition, data)) {
            return FALSE;
        }
    }

    return TRUE;
}

static void
update_socket_watch(void)
{
    gboolean out = socket_to_send->len > 0;

    if (socket_tag != 0) {
        g_source_remove(socket_tag);
    }

    socket_tag = g_io_add_watch(channel_socket,
        G_IO_IN | (out ? G_IO_OUT : 0), do_socket, NULL);
}

static gboolean
do_command(GIOChannel *source,
           GIOCondition condition,
           gpointer data)
{
    char *string;
    VCardEmulError error;
    static unsigned int default_reader_id;
    unsigned int reader_id;
    VReader *reader = NULL;
    GError *err = NULL;

    g_assert(condition & G_IO_IN);

    reader_id = default_reader_id;
    g_io_channel_read_line(source, &string, NULL, NULL, &err);
    if (err != NULL) {
        g_error("Error while reading command: %s", err->message);
    }

    if (string != NULL) {
        if (strncmp(string, "exit", 4) == 0) {
            /* remove all the readers */
            VReaderList *list = vreader_get_reader_list();
            VReaderListEntry *reader_entry;
            printf("Active Readers:\n");
            for (reader_entry = vreader_list_get_first(list); reader_entry;
                 reader_entry = vreader_list_get_next(reader_entry)) {
                VReader *reader = vreader_list_get_reader(reader_entry);
                vreader_id_t reader_id;
                reader_id = vreader_get_id(reader);
                if (reader_id == -1) {
                    continue;
                }
                /* be nice and signal card removal first (qemu probably should
                 * do this itself) */
                if (vreader_card_is_present(reader) == VREADER_OK) {
                    send_msg(VSC_CardRemove, reader_id, NULL, 0);
                }
                send_msg(VSC_ReaderRemove, reader_id, NULL, 0);
            }
            exit(0);
        } else if (strncmp(string, "insert", 6) == 0) {
            if (string[6] == ' ') {
                reader_id = get_id_from_string(&string[7], reader_id);
            }
            reader = vreader_get_reader_by_id(reader_id);
            if (reader != NULL) {
                error = vcard_emul_force_card_insert(reader);
                printf("insert %s, returned %d\n",
                       reader ? vreader_get_name(reader)
                       : "invalid reader", error);
            } else {
                printf("no reader by id %u found\n", reader_id);
            }
        } else if (strncmp(string, "remove", 6) == 0) {
            if (string[6] == ' ') {
                reader_id = get_id_from_string(&string[7], reader_id);
            }
            reader = vreader_get_reader_by_id(reader_id);
            if (reader != NULL) {
                error = vcard_emul_force_card_remove(reader);
                printf("remove %s, returned %d\n",
                        reader ? vreader_get_name(reader)
                        : "invalid reader", error);
            } else {
                printf("no reader by id %u found\n", reader_id);
            }
        } else if (strncmp(string, "select", 6) == 0) {
            if (string[6] == ' ') {
                reader_id = get_id_from_string(&string[7],
                                               VSCARD_UNDEFINED_READER_ID);
            }
            if (reader_id != VSCARD_UNDEFINED_READER_ID) {
                reader = vreader_get_reader_by_id(reader_id);
            }
            if (reader) {
                printf("Selecting reader %u, %s\n", reader_id,
                        vreader_get_name(reader));
                default_reader_id = reader_id;
            } else {
                printf("Reader with id %u not found\n", reader_id);
            }
        } else if (strncmp(string, "debug", 5) == 0) {
            if (string[5] == ' ') {
                verbose = get_id_from_string(&string[6], 0);
            }
            printf("debug level = %d\n", verbose);
        } else if (strncmp(string, "list", 4) == 0) {
            VReaderList *list = vreader_get_reader_list();
            VReaderListEntry *reader_entry;
            printf("Active Readers:\n");
            for (reader_entry = vreader_list_get_first(list); reader_entry;
                 reader_entry = vreader_list_get_next(reader_entry)) {
                VReader *reader = vreader_list_get_reader(reader_entry);
                vreader_id_t reader_id;
                reader_id = vreader_get_id(reader);
                if (reader_id == -1) {
                    continue;
                }
                printf("%3u %s %s\n", reader_id,
                       vreader_card_is_present(reader) == VREADER_OK ?
                       "CARD_PRESENT" : "            ",
                       vreader_get_name(reader));
            }
            printf("Inactive Readers:\n");
            for (reader_entry = vreader_list_get_first(list); reader_entry;
                 reader_entry = vreader_list_get_next(reader_entry)) {
                VReader *reader = vreader_list_get_reader(reader_entry);
                vreader_id_t reader_id;
                reader_id = vreader_get_id(reader);
                if (reader_id != -1) {
                    continue;
                }

                printf("INA %s %s\n",
                       vreader_card_is_present(reader) == VREADER_OK ?
                       "CARD_PRESENT" : "            ",
                       vreader_get_name(reader));
            }
        } else if (*string != 0) {
            printf("valid commands:\n");
            printf("insert [reader_id]\n");
            printf("remove [reader_id]\n");
            printf("select reader_id\n");
            printf("list\n");
            printf("debug [level]\n");
            printf("exit\n");
        }
    }
    vreader_free(reader);
    printf("> ");
    fflush(stdout);

    return TRUE;
}


/* just for ease of parsing command line arguments. */
#define MAX_CERTS 100

static int
connect_to_qemu(
    const char *host,
    const char *port
) {
    struct addrinfo hints;
    struct addrinfo *server;
    int ret, sock;

    sock = qemu_socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        /* Error */
        fprintf(stderr, "Error opening socket!\n");
        return -1;
    }

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = 0;
    hints.ai_protocol = 0;          /* Any protocol */

    ret = getaddrinfo(host, port, &hints, &server);

    if (ret != 0) {
        /* Error */
        fprintf(stderr, "getaddrinfo failed\n");
        goto cleanup_socket;
    }

    if (connect(sock, server->ai_addr, server->ai_addrlen) < 0) {
        /* Error */
        fprintf(stderr, "Could not connect\n");
        goto cleanup_socket;
    }
    if (verbose) {
        printf("Connected (sizeof Header=%zd)!\n", sizeof(VSCMsgHeader));
    }
    return sock;

cleanup_socket:
    closesocket(sock);
    return -1;
}

int
main(
    int argc,
    char *argv[]
) {
    GMainLoop *loop;
    GIOChannel *channel_stdin;
    char *qemu_host;
    char *qemu_port;

    VCardEmulOptions *command_line_options = NULL;

    char *cert_names[MAX_CERTS];
    char *emul_args = NULL;
    int cert_count = 0;
    int c, sock;

    if (socket_init() != 0)
        return 1;

    while ((c = getopt(argc, argv, "c:e:pd:")) != -1) {
        switch (c) {
        case 'c':
            if (cert_count >= MAX_CERTS) {
                printf("too many certificates (max = %d)\n", MAX_CERTS);
                exit(5);
            }
            cert_names[cert_count++] = optarg;
            break;
        case 'e':
            emul_args = optarg;
            break;
        case 'p':
            print_usage();
            exit(4);
            break;
        case 'd':
            verbose = get_id_from_string(optarg, 1);
            break;
        }
    }

    if (argc - optind != 2) {
        print_usage();
        exit(4);
    }

    if (cert_count > 0) {
        char *new_args;
        int len, i;
        /* if we've given some -c options, we clearly we want do so some
         * software emulation.  add that emulation now. this is NSS Emulator
         * specific */
        if (emul_args == NULL) {
            emul_args = (char *)"db=\"/etc/pki/nssdb\"";
        }
#define SOFT_STRING ",soft=(,Virtual Reader,CAC,,"
             /* 2 == close paren & null */
        len = strlen(emul_args) + strlen(SOFT_STRING) + 2;
        for (i = 0; i < cert_count; i++) {
            len += strlen(cert_names[i])+1; /* 1 == comma */
        }
        new_args = g_malloc(len);
        strcpy(new_args, emul_args);
        strcat(new_args, SOFT_STRING);
        for (i = 0; i < cert_count; i++) {
            strcat(new_args, cert_names[i]);
            strcat(new_args, ",");
        }
        strcat(new_args, ")");
        emul_args = new_args;
    }
    if (emul_args) {
        command_line_options = vcard_emul_options(emul_args);
    }

    qemu_host = g_strdup(argv[argc - 2]);
    qemu_port = g_strdup(argv[argc - 1]);
    sock = connect_to_qemu(qemu_host, qemu_port);
    if (sock == -1) {
        fprintf(stderr, "error opening socket, exiting.\n");
        exit(5);
    }

    socket_to_send = g_byte_array_new();
    qemu_mutex_init(&socket_to_send_lock);
    qemu_mutex_init(&pending_reader_lock);
    qemu_cond_init(&pending_reader_condition);

    vcard_emul_init(command_line_options);

    loop = g_main_loop_new(NULL, true);

    printf("> ");
    fflush(stdout);

#ifdef _WIN32
    channel_stdin = g_io_channel_win32_new_fd(STDIN_FILENO);
#else
    channel_stdin = g_io_channel_unix_new(STDIN_FILENO);
#endif
    g_io_add_watch(channel_stdin, G_IO_IN, do_command, NULL);
#ifdef _WIN32
    channel_socket = g_io_channel_win32_new_socket(sock);
#else
    channel_socket = g_io_channel_unix_new(sock);
#endif
    g_io_channel_set_encoding(channel_socket, NULL, NULL);
    /* we buffer ourself for thread safety reasons */
    g_io_channel_set_buffered(channel_socket, FALSE);

    /* Send init message, Host responds (and then we send reader attachments) */
    VSCMsgInit init = {
        .version = htonl(VSCARD_VERSION),
        .magic = VSCARD_MAGIC,
        .capabilities = {0}
    };
    send_msg(VSC_Init, 0, &init, sizeof(init));

    g_main_loop_run(loop);
    g_main_loop_unref(loop);

    g_io_channel_unref(channel_stdin);
    g_io_channel_unref(channel_socket);
    g_byte_array_free(socket_to_send, TRUE);

    closesocket(sock);
    return 0;
}
