#ifndef HW_HYPERV_DYNMEM_PROTO_H
#define HW_HYPERV_DYNMEM_PROTO_H

/*
 * Hyper-V Dynamic Memory Protocol definitions
 *
 * Copyright (C) 2020-2023 Oracle and/or its affiliates.
 *
 * Based on drivers/hv/hv_balloon.c from Linux kernel:
 * Copyright (c) 2012, Microsoft Corporation.
 *
 * Author: K. Y. Srinivasan <kys@microsoft.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

/*
 * Protocol versions. The low word is the minor version, the high word the major
 * version.
 *
 * History:
 * Initial version 1.0
 * Changed to 0.1 on 2009/03/25
 * Changes to 0.2 on 2009/05/14
 * Changes to 0.3 on 2009/12/03
 * Changed to 1.0 on 2011/04/05
 * Changed to 2.0 on 2019/12/10
 */

#define DYNMEM_MAKE_VERSION(Major, Minor) ((uint32_t)(((Major) << 16) | (Minor)))
#define DYNMEM_MAJOR_VERSION(Version) ((uint32_t)(Version) >> 16)
#define DYNMEM_MINOR_VERSION(Version) ((uint32_t)(Version) & 0xff)

enum {
    DYNMEM_PROTOCOL_VERSION_1 = DYNMEM_MAKE_VERSION(0, 3),
    DYNMEM_PROTOCOL_VERSION_2 = DYNMEM_MAKE_VERSION(1, 0),
    DYNMEM_PROTOCOL_VERSION_3 = DYNMEM_MAKE_VERSION(2, 0),

    DYNMEM_PROTOCOL_VERSION_WIN7 = DYNMEM_PROTOCOL_VERSION_1,
    DYNMEM_PROTOCOL_VERSION_WIN8 = DYNMEM_PROTOCOL_VERSION_2,
    DYNMEM_PROTOCOL_VERSION_WIN10 = DYNMEM_PROTOCOL_VERSION_3,

    DYNMEM_PROTOCOL_VERSION_CURRENT = DYNMEM_PROTOCOL_VERSION_WIN10
};



/*
 * Message Types
 */

enum dm_message_type {
    /*
     * Version 0.3
     */
    DM_ERROR = 0,
    DM_VERSION_REQUEST = 1,
    DM_VERSION_RESPONSE = 2,
    DM_CAPABILITIES_REPORT = 3,
    DM_CAPABILITIES_RESPONSE = 4,
    DM_STATUS_REPORT = 5,
    DM_BALLOON_REQUEST = 6,
    DM_BALLOON_RESPONSE = 7,
    DM_UNBALLOON_REQUEST = 8,
    DM_UNBALLOON_RESPONSE = 9,
    DM_MEM_HOT_ADD_REQUEST = 10,
    DM_MEM_HOT_ADD_RESPONSE = 11,
    DM_VERSION_03_MAX = 11,
    /*
     * Version 1.0.
     */
    DM_INFO_MESSAGE = 12,
    DM_VERSION_1_MAX = 12,

    /*
     * Version 2.0
     */
    DM_MEM_HOT_REMOVE_REQUEST = 13,
    DM_MEM_HOT_REMOVE_RESPONSE = 14
};


/*
 * Structures defining the dynamic memory management
 * protocol.
 */

union dm_version {
    struct {
        uint16_t minor_version;
        uint16_t major_version;
    };
    uint32_t version;
} QEMU_PACKED;


union dm_caps {
    struct {
        uint64_t balloon:1;
        uint64_t hot_add:1;
        /*
         * To support guests that may have alignment
         * limitations on hot-add, the guest can specify
         * its alignment requirements; a value of n
         * represents an alignment of 2^n in mega bytes.
         */
        uint64_t hot_add_alignment:4;
        uint64_t hot_remove:1;
        uint64_t reservedz:57;
    } cap_bits;
    uint64_t caps;
} QEMU_PACKED;

union dm_mem_page_range {
    struct  {
        /*
         * The PFN number of the first page in the range.
         * 40 bits is the architectural limit of a PFN
         * number for AMD64.
         */
        uint64_t start_page:40;
        /*
         * The number of pages in the range.
         */
        uint64_t page_cnt:24;
    } finfo;
    uint64_t  page_range;
} QEMU_PACKED;



/*
 * The header for all dynamic memory messages:
 *
 * type: Type of the message.
 * size: Size of the message in bytes; including the header.
 * trans_id: The guest is responsible for manufacturing this ID.
 */

struct dm_header {
    uint16_t type;
    uint16_t size;
    uint32_t trans_id;
} QEMU_PACKED;

/*
 * A generic message format for dynamic memory.
 * Specific message formats are defined later in the file.
 */

struct dm_message {
    struct dm_header hdr;
    uint8_t data[]; /* enclosed message */
} QEMU_PACKED;


/*
 * Specific message types supporting the dynamic memory protocol.
 */

/*
 * Version negotiation message. Sent from the guest to the host.
 * The guest is free to try different versions until the host
 * accepts the version.
 *
 * dm_version: The protocol version requested.
 * is_last_attempt: If TRUE, this is the last version guest will request.
 * reservedz: Reserved field, set to zero.
 */

struct dm_version_request {
    struct dm_header hdr;
    union dm_version version;
    uint32_t is_last_attempt:1;
    uint32_t reservedz:31;
} QEMU_PACKED;

/*
 * Version response message; Host to Guest and indicates
 * if the host has accepted the version sent by the guest.
 *
 * is_accepted: If TRUE, host has accepted the version and the guest
 * should proceed to the next stage of the protocol. FALSE indicates that
 * guest should re-try with a different version.
 *
 * reservedz: Reserved field, set to zero.
 */

struct dm_version_response {
    struct dm_header hdr;
    uint64_t is_accepted:1;
    uint64_t reservedz:63;
} QEMU_PACKED;

/*
 * Message reporting capabilities. This is sent from the guest to the
 * host.
 */

struct dm_capabilities {
    struct dm_header hdr;
    union dm_caps caps;
    uint64_t min_page_cnt;
    uint64_t max_page_number;
} QEMU_PACKED;

/*
 * Response to the capabilities message. This is sent from the host to the
 * guest. This message notifies if the host has accepted the guest's
 * capabilities. If the host has not accepted, the guest must shutdown
 * the service.
 *
 * is_accepted: Indicates if the host has accepted guest's capabilities.
 * reservedz: Must be 0.
 */

struct dm_capabilities_resp_msg {
    struct dm_header hdr;
    uint64_t is_accepted:1;
    uint64_t hot_remove:1;
    uint64_t suppress_pressure_reports:1;
    uint64_t reservedz:61;
} QEMU_PACKED;

/*
 * This message is used to report memory pressure from the guest.
 * This message is not part of any transaction and there is no
 * response to this message.
 *
 * num_avail: Available memory in pages.
 * num_committed: Committed memory in pages.
 * page_file_size: The accumulated size of all page files
 *                 in the system in pages.
 * zero_free: The number of zero and free pages.
 * page_file_writes: The writes to the page file in pages.
 * io_diff: An indicator of file cache efficiency or page file activity,
 *          calculated as File Cache Page Fault Count - Page Read Count.
 *          This value is in pages.
 *
 * Some of these metrics are Windows specific and fortunately
 * the algorithm on the host side that computes the guest memory
 * pressure only uses num_committed value.
 */

struct dm_status {
    struct dm_header hdr;
    uint64_t num_avail;
    uint64_t num_committed;
    uint64_t page_file_size;
    uint64_t zero_free;
    uint32_t page_file_writes;
    uint32_t io_diff;
} QEMU_PACKED;


/*
 * Message to ask the guest to allocate memory - balloon up message.
 * This message is sent from the host to the guest. The guest may not be
 * able to allocate as much memory as requested.
 *
 * num_pages: number of pages to allocate.
 */

struct dm_balloon {
    struct dm_header hdr;
    uint32_t num_pages;
    uint32_t reservedz;
} QEMU_PACKED;


/*
 * Balloon response message; this message is sent from the guest
 * to the host in response to the balloon message.
 *
 * reservedz: Reserved; must be set to zero.
 * more_pages: If FALSE, this is the last message of the transaction.
 * if TRUE there will be at least one more message from the guest.
 *
 * range_count: The number of ranges in the range array.
 *
 * range_array: An array of page ranges returned to the host.
 *
 */

struct dm_balloon_response {
    struct dm_header hdr;
    uint32_t reservedz;
    uint32_t more_pages:1;
    uint32_t range_count:31;
    union dm_mem_page_range range_array[];
} QEMU_PACKED;

/*
 * Un-balloon message; this message is sent from the host
 * to the guest to give guest more memory.
 *
 * more_pages: If FALSE, this is the last message of the transaction.
 * if TRUE there will be at least one more message from the guest.
 *
 * reservedz: Reserved; must be set to zero.
 *
 * range_count: The number of ranges in the range array.
 *
 * range_array: An array of page ranges returned to the host.
 *
 */

struct dm_unballoon_request {
    struct dm_header hdr;
    uint32_t more_pages:1;
    uint32_t reservedz:31;
    uint32_t range_count;
    union dm_mem_page_range range_array[];
} QEMU_PACKED;

/*
 * Un-balloon response message; this message is sent from the guest
 * to the host in response to an unballoon request.
 *
 */

struct dm_unballoon_response {
    struct dm_header hdr;
} QEMU_PACKED;


/*
 * Hot add request message. Message sent from the host to the guest.
 *
 * mem_range: Memory range to hot add.
 *
 */

struct dm_hot_add {
    struct dm_header hdr;
    union dm_mem_page_range range;
} QEMU_PACKED;

/*
 * Hot add response message.
 * This message is sent by the guest to report the status of a hot add request.
 * If page_count is less than the requested page count, then the host should
 * assume all further hot add requests will fail, since this indicates that
 * the guest has hit an upper physical memory barrier.
 *
 * Hot adds may also fail due to low resources; in this case, the guest must
 * not complete this message until the hot add can succeed, and the host must
 * not send a new hot add request until the response is sent.
 * If VSC fails to hot add memory DYNMEM_NUMBER_OF_UNSUCCESSFUL_HOTADD_ATTEMPTS
 * times it fails the request.
 *
 *
 * page_count: number of pages that were successfully hot added.
 *
 * result: result of the operation 1: success, 0: failure.
 *
 */

struct dm_hot_add_response {
    struct dm_header hdr;
    uint32_t page_count;
    uint32_t result;
} QEMU_PACKED;

struct dm_hot_remove {
    struct dm_header hdr;
    uint32_t virtual_node;
    uint32_t page_count;
    uint32_t qos_flags;
    uint32_t reservedZ;
} QEMU_PACKED;

struct dm_hot_remove_response {
    struct dm_header hdr;
    uint32_t result;
    uint32_t range_count;
    uint64_t more_pages:1;
    uint64_t reservedz:63;
    union dm_mem_page_range range_array[];
} QEMU_PACKED;

#define DM_REMOVE_QOS_LARGE (1 << 0)
#define DM_REMOVE_QOS_LOCAL (1 << 1)
#define DM_REMOVE_QOS_MASK (0x3)

/*
 * Types of information sent from host to the guest.
 */

enum dm_info_type {
    INFO_TYPE_MAX_PAGE_CNT = 0,
    MAX_INFO_TYPE
};


/*
 * Header for the information message.
 */

struct dm_info_header {
    enum dm_info_type type;
    uint32_t data_size;
    uint8_t  data[];
} QEMU_PACKED;

/*
 * This message is sent from the host to the guest to pass
 * some relevant information (win8 addition).
 *
 * reserved: no used.
 * info_size: size of the information blob.
 * info: information blob.
 */

struct dm_info_msg {
    struct dm_header hdr;
    uint32_t reserved;
    uint32_t info_size;
    uint8_t  info[];
};

#endif
