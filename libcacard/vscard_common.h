/* Virtual Smart Card protocol definition
 *
 * This protocol is between a host using virtual smart card readers,
 * and a client providing the smart cards, perhaps by emulating them or by
 * access to real cards.
 *
 * Definitions for this protocol:
 *  Host   - user of the card
 *  Client - owner of the card
 *
 * The current implementation passes the raw APDU's from 7816 and additionally
 * contains messages to setup and teardown readers, handle insertion and
 * removal of cards, negotiate the protocol via capabilities and provide
 * for error responses.
 *
 * Copyright (c) 2011 Red Hat.
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#ifndef VSCARD_COMMON_H
#define VSCARD_COMMON_H

#include <stdint.h>

#define VERSION_MAJOR_BITS 11
#define VERSION_MIDDLE_BITS 11
#define VERSION_MINOR_BITS 10

#define MAKE_VERSION(major, middle, minor) \
     ((major  << (VERSION_MINOR_BITS + VERSION_MIDDLE_BITS)) \
      | (middle <<  VERSION_MINOR_BITS) \
      | (minor))

/*
 * IMPORTANT NOTE on VERSION
 *
 * The version below MUST be changed whenever a change in this file is made.
 *
 * The last digit, the minor, is for bug fix changes only.
 *
 * The middle digit is for backward / forward compatible changes, updates
 * to the existing messages, addition of fields.
 *
 * The major digit is for a breaking change of protocol, presumably
 * something that cannot be accommodated with the existing protocol.
 */

#define VSCARD_VERSION MAKE_VERSION(0, 0, 2)

typedef enum VSCMsgType {
    VSC_Init = 1,
    VSC_Error,
    VSC_ReaderAdd,
    VSC_ReaderRemove,
    VSC_ATR,
    VSC_CardRemove,
    VSC_APDU,
    VSC_Flush,
    VSC_FlushComplete
} VSCMsgType;

typedef enum VSCErrorCode {
    VSC_SUCCESS = 0,
    VSC_GENERAL_ERROR = 1,
    VSC_CANNOT_ADD_MORE_READERS,
    VSC_CARD_ALREAY_INSERTED,
} VSCErrorCode;

#define VSCARD_UNDEFINED_READER_ID  0xffffffff
#define VSCARD_MINIMAL_READER_ID    0

#define VSCARD_MAGIC (*(uint32_t *)"VSCD")

/*
 * Header
 * Each message starts with the header.
 * type - message type
 * reader_id - used by messages that are reader specific
 * length - length of payload (not including header, i.e. zero for
 *  messages containing empty payloads)
 */
typedef struct VSCMsgHeader {
    uint32_t   type;
    uint32_t   reader_id;
    uint32_t   length;
    uint8_t    data[0];
} VSCMsgHeader;

/*
 * VSCMsgInit               Client <-> Host
 * Client sends it on connection, with its own capabilities.
 * Host replies with VSCMsgInit filling in its capabilities.
 *
 * It is not meant to be used for negotiation, i.e. sending more then
 * once from any side, but could be used for that in the future.
 */
typedef struct VSCMsgInit {
    uint32_t   magic;
    uint32_t   version;
    uint32_t   capabilities[1]; /* receiver must check length,
                                   array may grow in the future*/
} VSCMsgInit;

/*
 * VSCMsgError              Client <-> Host
 * This message is a response to any of:
 *  Reader Add
 *  Reader Remove
 *  Card Remove
 * If the operation was successful then VSC_SUCCESS
 * is returned, other wise a specific error code.
 */
typedef struct VSCMsgError {
    uint32_t   code;
} VSCMsgError;

/*
 * VSCMsgReaderAdd          Client -> Host
 * Host replies with allocated reader id in VSCMsgError with code==SUCCESS.
 *
 * name - name of the reader on client side, UTF-8 encoded. Only used
 *  for client presentation (may be translated to the device presented to the
 *  guest), protocol wise only reader_id is important.
 */
typedef struct VSCMsgReaderAdd {
    uint8_t    name[0];
} VSCMsgReaderAdd;

/*
 * VSCMsgReaderRemove       Client -> Host
 * The client's reader has been removed.
 */
typedef struct VSCMsgReaderRemove {
} VSCMsgReaderRemove;

/*
 * VSCMsgATR                Client -> Host
 * Answer to reset. Sent for card insertion or card reset. The reset/insertion
 * happens on the client side, they do not require any action from the host.
 */
typedef struct VSCMsgATR {
    uint8_t     atr[0];
} VSCMsgATR;

/*
 * VSCMsgCardRemove         Client -> Host
 * The client card has been removed.
 */
typedef struct VSCMsgCardRemove {
} VSCMsgCardRemove;

/*
 * VSCMsgAPDU               Client <-> Host
 * Main reason of existence. Transfer a single APDU in either direction.
 */
typedef struct VSCMsgAPDU {
    uint8_t    data[0];
} VSCMsgAPDU;

/*
 * VSCMsgFlush               Host -> Client
 * Request client to send a FlushComplete message when it is done
 * servicing all outstanding APDUs
 */
typedef struct VSCMsgFlush {
} VSCMsgFlush;

/*
 * VSCMsgFlush               Client -> Host
 * Client response to Flush after all APDUs have been processed and
 * responses sent.
 */
typedef struct VSCMsgFlushComplete {
} VSCMsgFlushComplete;

#endif /* VSCARD_COMMON_H */
