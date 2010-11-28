/*
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#ifndef VREADERT_H
#define VREADERT_H 1

typedef enum {
    VREADER_OK = 0,
    VREADER_NO_CARD,
    VREADER_OUT_OF_MEMORY
} VReaderStatus;

typedef unsigned int vreader_id_t;
typedef struct VReaderStruct VReader;
typedef struct VReaderListStruct VReaderList;
typedef struct VReaderListEntryStruct VReaderListEntry;

typedef struct VReaderEmulStruct VReaderEmul;
typedef void (*VReaderEmulFree)(VReaderEmul *);

#endif

