/*
 * Service Discover Protocol server for QEMU L2CAP devices
 *
 * Copyright (C) 2008 Andrzej Zaborowski  <balrog@zabor.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "qemu-common.h"
#include "bt.h"

struct bt_l2cap_sdp_state_s {
    struct bt_l2cap_conn_params_s *channel;

    struct sdp_service_record_s {
        int match;

        int *uuid;
        int uuids;
        struct sdp_service_attribute_s {
            int match;

            int attribute_id;
            int len;
            void *pair;
        } *attribute_list;
        int attributes;
    } *service_list;
    int services;
};

static ssize_t sdp_datalen(const uint8_t **element, ssize_t *left)
{
    size_t len = *(*element) ++ & SDP_DSIZE_MASK;

    if (!*left)
        return -1;
    (*left) --;

    if (len < SDP_DSIZE_NEXT1)
        return 1 << len;
    else if (len == SDP_DSIZE_NEXT1) {
        if (*left < 1)
            return -1;
        (*left) --;

        return *(*element) ++;
    } else if (len == SDP_DSIZE_NEXT2) {
        if (*left < 2)
            return -1;
        (*left) -= 2;

        len = (*(*element) ++) << 8;
        return len | (*(*element) ++);
    } else {
        if (*left < 4)
            return -1;
        (*left) -= 4;

        len = (*(*element) ++) << 24;
        len |= (*(*element) ++) << 16;
        len |= (*(*element) ++) << 8;
        return len | (*(*element) ++);
    }
}

static const uint8_t bt_base_uuid[12] = {
    0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb,
};

static int sdp_uuid_match(struct sdp_service_record_s *record,
                const uint8_t *uuid, ssize_t datalen)
{
    int *lo, hi, val;

    if (datalen == 16 || datalen == 4) {
        if (datalen == 16 && memcmp(uuid + 4, bt_base_uuid, 12))
            return 0;

        if (uuid[0] | uuid[1])
            return 0;
        uuid += 2;
    }

    val = (uuid[0] << 8) | uuid[1];
    lo = record->uuid;
    hi = record->uuids;
    while (hi >>= 1)
        if (lo[hi] <= val)
            lo += hi;

    return *lo == val;
}

#define CONTINUATION_PARAM_SIZE	(1 + sizeof(int))
#define MAX_PDU_OUT_SIZE	96	/* Arbitrary */
#define PDU_HEADER_SIZE		5
#define MAX_RSP_PARAM_SIZE	(MAX_PDU_OUT_SIZE - PDU_HEADER_SIZE - \
                CONTINUATION_PARAM_SIZE)

static int sdp_svc_match(struct bt_l2cap_sdp_state_s *sdp,
                const uint8_t **req, ssize_t *len)
{
    size_t datalen;
    int i;

    if ((**req & ~SDP_DSIZE_MASK) != SDP_DTYPE_UUID)
        return 1;

    datalen = sdp_datalen(req, len);
    if (datalen != 2 && datalen != 4 && datalen != 16)
        return 1;

    for (i = 0; i < sdp->services; i ++)
        if (sdp_uuid_match(&sdp->service_list[i], *req, datalen))
            sdp->service_list[i].match = 1;

    (*req) += datalen;
    (*len) -= datalen;

    return 0;
}

static ssize_t sdp_svc_search(struct bt_l2cap_sdp_state_s *sdp,
                uint8_t *rsp, const uint8_t *req, ssize_t len)
{
    ssize_t seqlen;
    int i, count, start, end, max;
    int32_t handle;

    /* Perform the search */
    for (i = 0; i < sdp->services; i ++)
        sdp->service_list[i].match = 0;

    if (len < 1)
        return -SDP_INVALID_SYNTAX;
    if ((*req & ~SDP_DSIZE_MASK) == SDP_DTYPE_SEQ) {
        seqlen = sdp_datalen(&req, &len);
        if (seqlen < 3 || len < seqlen)
            return -SDP_INVALID_SYNTAX;
        len -= seqlen;

        while (seqlen)
            if (sdp_svc_match(sdp, &req, &seqlen))
                return -SDP_INVALID_SYNTAX;
    } else if (sdp_svc_match(sdp, &req, &seqlen))
        return -SDP_INVALID_SYNTAX;

    if (len < 3)
        return -SDP_INVALID_SYNTAX;
    end = (req[0] << 8) | req[1];
    req += 2;
    len -= 2;

    if (*req) {
        if (len <= sizeof(int))
            return -SDP_INVALID_SYNTAX;
        len -= sizeof(int);
        memcpy(&start, req + 1, sizeof(int));
    } else
        start = 0;

    if (len > 1);
        return -SDP_INVALID_SYNTAX;

    /* Output the results */
    len = 4;
    count = 0;
    end = start;
    for (i = 0; i < sdp->services; i ++)
        if (sdp->service_list[i].match) {
            if (count >= start && count < max && len + 4 < MAX_RSP_PARAM_SIZE) {
                handle = i;
                memcpy(rsp + len, &handle, 4);
                len += 4;
                end = count + 1;
            }

            count ++;
        }

    rsp[0] = count >> 8;
    rsp[1] = count & 0xff;
    rsp[2] = (end - start) >> 8;
    rsp[3] = (end - start) & 0xff;

    if (end < count) {
        rsp[len ++] = sizeof(int);
        memcpy(rsp + len, &end, sizeof(int));
        len += 4;
    } else
        rsp[len ++] = 0;

    return len;
}

static int sdp_attr_match(struct sdp_service_record_s *record,
                const uint8_t **req, ssize_t *len)
{
    int i, start, end;

    if (**req == (SDP_DTYPE_UINT | SDP_DSIZE_2)) {
        (*req) ++;
        if (*len < 3)
            return 1;

        start = (*(*req) ++) << 8;
        start |= *(*req) ++;
        end = start;
        *len -= 3;
    } else if (**req == (SDP_DTYPE_UINT | SDP_DSIZE_4)) {
        (*req) ++;
        if (*len < 5)
            return 1;

        start = (*(*req) ++) << 8;
        start |= *(*req) ++;
        end = (*(*req) ++) << 8;
        end |= *(*req) ++;
        *len -= 5;
    } else
        return 1;

    for (i = 0; i < record->attributes; i ++)
        if (record->attribute_list[i].attribute_id >= start &&
                        record->attribute_list[i].attribute_id <= end)
            record->attribute_list[i].match = 1;

    return 0;
}

static ssize_t sdp_attr_get(struct bt_l2cap_sdp_state_s *sdp,
                uint8_t *rsp, const uint8_t *req, ssize_t len)
{
    ssize_t seqlen;
    int i, start, end, max;
    int32_t handle;
    struct sdp_service_record_s *record;
    uint8_t *lst;

    /* Perform the search */
    if (len < 7)
        return -SDP_INVALID_SYNTAX;
    memcpy(&handle, req, 4);
    req += 4;
    len -= 4;

    if (handle < 0 || handle > sdp->services)
        return -SDP_INVALID_RECORD_HANDLE;
    record = &sdp->service_list[handle];

    for (i = 0; i < record->attributes; i ++)
        record->attribute_list[i].match = 0;

    max = (req[0] << 8) | req[1];
    req += 2;
    len -= 2;
    if (max < 0x0007)
        return -SDP_INVALID_SYNTAX;

    if ((*req & ~SDP_DSIZE_MASK) == SDP_DTYPE_SEQ) {
        seqlen = sdp_datalen(&req, &len);
        if (seqlen < 3 || len < seqlen)
            return -SDP_INVALID_SYNTAX;
        len -= seqlen;

        while (seqlen)
            if (sdp_attr_match(record, &req, &seqlen))
                return -SDP_INVALID_SYNTAX;
    } else if (sdp_attr_match(record, &req, &seqlen))
        return -SDP_INVALID_SYNTAX;

    if (len < 1)
        return -SDP_INVALID_SYNTAX;

    if (*req) {
        if (len <= sizeof(int))
            return -SDP_INVALID_SYNTAX;
        len -= sizeof(int);
        memcpy(&start, req + 1, sizeof(int));
    } else
        start = 0;

    if (len > 1)
        return -SDP_INVALID_SYNTAX;

    /* Output the results */
    lst = rsp + 2;
    max = MIN(max, MAX_RSP_PARAM_SIZE);
    len = 3 - start;
    end = 0;
    for (i = 0; i < record->attributes; i ++)
        if (record->attribute_list[i].match) {
            if (len >= 0 && len + record->attribute_list[i].len < max) {
                memcpy(lst + len, record->attribute_list[i].pair,
                                record->attribute_list[i].len);
                end = len + record->attribute_list[i].len;
            }
            len += record->attribute_list[i].len;
        }
    if (0 >= start) {
       lst[0] = SDP_DTYPE_SEQ | SDP_DSIZE_NEXT2;
       lst[1] = (len + start - 3) >> 8;
       lst[2] = (len + start - 3) & 0xff;
    }

    rsp[0] = end >> 8;
    rsp[1] = end & 0xff;

    if (end < len) {
        len = end + start;
        lst[end ++] = sizeof(int);
        memcpy(lst + end, &len, sizeof(int));
        end += sizeof(int);
    } else
        lst[end ++] = 0;

    return end + 2;
}

static int sdp_svc_attr_match(struct bt_l2cap_sdp_state_s *sdp,
                const uint8_t **req, ssize_t *len)
{
    int i, j, start, end;
    struct sdp_service_record_s *record;

    if (**req == (SDP_DTYPE_UINT | SDP_DSIZE_2)) {
        (*req) ++;
        if (*len < 3)
            return 1;

        start = (*(*req) ++) << 8;
        start |= *(*req) ++;
        end = start;
        *len -= 3;
    } else if (**req == (SDP_DTYPE_UINT | SDP_DSIZE_4)) {
        (*req) ++;
        if (*len < 5)
            return 1;

        start = (*(*req) ++) << 8;
        start |= *(*req) ++;
        end = (*(*req) ++) << 8;
        end |= *(*req) ++;
        *len -= 5;
    } else
        return 1;

    for (i = 0; i < sdp->services; i ++)
        if ((record = &sdp->service_list[i])->match)
            for (j = 0; j < record->attributes; j ++)
                if (record->attribute_list[j].attribute_id >= start &&
                                record->attribute_list[j].attribute_id <= end)
                    record->attribute_list[j].match = 1;

    return 0;
}

static ssize_t sdp_svc_search_attr_get(struct bt_l2cap_sdp_state_s *sdp,
                uint8_t *rsp, const uint8_t *req, ssize_t len)
{
    ssize_t seqlen;
    int i, j, start, end, max;
    struct sdp_service_record_s *record;
    uint8_t *lst;

    /* Perform the search */
    for (i = 0; i < sdp->services; i ++) {
        sdp->service_list[i].match = 0;
            for (j = 0; j < sdp->service_list[i].attributes; j ++)
                sdp->service_list[i].attribute_list[j].match = 0;
    }

    if (len < 1)
        return -SDP_INVALID_SYNTAX;
    if ((*req & ~SDP_DSIZE_MASK) == SDP_DTYPE_SEQ) {
        seqlen = sdp_datalen(&req, &len);
        if (seqlen < 3 || len < seqlen)
            return -SDP_INVALID_SYNTAX;
        len -= seqlen;

        while (seqlen)
            if (sdp_svc_match(sdp, &req, &seqlen))
                return -SDP_INVALID_SYNTAX;
    } else if (sdp_svc_match(sdp, &req, &seqlen))
        return -SDP_INVALID_SYNTAX;

    if (len < 3)
        return -SDP_INVALID_SYNTAX;
    max = (req[0] << 8) | req[1];
    req += 2;
    len -= 2;
    if (max < 0x0007)
        return -SDP_INVALID_SYNTAX;

    if ((*req & ~SDP_DSIZE_MASK) == SDP_DTYPE_SEQ) {
        seqlen = sdp_datalen(&req, &len);
        if (seqlen < 3 || len < seqlen)
            return -SDP_INVALID_SYNTAX;
        len -= seqlen;

        while (seqlen)
            if (sdp_svc_attr_match(sdp, &req, &seqlen))
                return -SDP_INVALID_SYNTAX;
    } else if (sdp_svc_attr_match(sdp, &req, &seqlen))
        return -SDP_INVALID_SYNTAX;

    if (len < 1)
        return -SDP_INVALID_SYNTAX;

    if (*req) {
        if (len <= sizeof(int))
            return -SDP_INVALID_SYNTAX;
        len -= sizeof(int);
        memcpy(&start, req + 1, sizeof(int));
    } else
        start = 0;

    if (len > 1)
        return -SDP_INVALID_SYNTAX;

    /* Output the results */
    /* This assumes empty attribute lists are never to be returned even
     * for matching Service Records.  In practice this shouldn't happen
     * as the requestor will usually include the always present
     * ServiceRecordHandle AttributeID in AttributeIDList.  */
    lst = rsp + 2;
    max = MIN(max, MAX_RSP_PARAM_SIZE);
    len = 3 - start;
    end = 0;
    for (i = 0; i < sdp->services; i ++)
        if ((record = &sdp->service_list[i])->match) {
            len += 3;
            seqlen = len;
            for (j = 0; j < record->attributes; j ++)
                if (record->attribute_list[j].match) {
                    if (len >= 0)
                        if (len + record->attribute_list[j].len < max) {
                            memcpy(lst + len, record->attribute_list[j].pair,
                                            record->attribute_list[j].len);
                            end = len + record->attribute_list[j].len;
                        }
                    len += record->attribute_list[j].len;
                }
            if (seqlen == len)
                len -= 3;
            else if (seqlen >= 3 && seqlen < max) {
                lst[seqlen - 3] = SDP_DTYPE_SEQ | SDP_DSIZE_NEXT2;
                lst[seqlen - 2] = (len - seqlen) >> 8;
                lst[seqlen - 1] = (len - seqlen) & 0xff;
            }
        }
    if (len == 3 - start)
        len -= 3;
    else if (0 >= start) {
       lst[0] = SDP_DTYPE_SEQ | SDP_DSIZE_NEXT2;
       lst[1] = (len + start - 3) >> 8;
       lst[2] = (len + start - 3) & 0xff;
    }

    rsp[0] = end >> 8;
    rsp[1] = end & 0xff;

    if (end < len) {
        len = end + start;
        lst[end ++] = sizeof(int);
        memcpy(lst + end, &len, sizeof(int));
        end += sizeof(int);
    } else
        lst[end ++] = 0;

    return end + 2;
}

static void bt_l2cap_sdp_sdu_in(void *opaque, const uint8_t *data, int len)
{
    struct bt_l2cap_sdp_state_s *sdp = opaque;
    enum bt_sdp_cmd pdu_id;
    uint8_t rsp[MAX_PDU_OUT_SIZE - PDU_HEADER_SIZE], *sdu_out;
    int transaction_id, plen;
    int err = 0;
    int rsp_len = 0;

    if (len < 5) {
        fprintf(stderr, "%s: short SDP PDU (%iB).\n", __FUNCTION__, len);
        return;
    }

    pdu_id = *data ++;
    transaction_id = (data[0] << 8) | data[1];
    plen = (data[2] << 8) | data[3];
    data += 4;
    len -= 5;

    if (len != plen) {
        fprintf(stderr, "%s: wrong SDP PDU length (%iB != %iB).\n",
                        __FUNCTION__, plen, len);
        err = SDP_INVALID_PDU_SIZE;
        goto respond;
    }

    switch (pdu_id) {
    case SDP_SVC_SEARCH_REQ:
        rsp_len = sdp_svc_search(sdp, rsp, data, len);
        pdu_id = SDP_SVC_SEARCH_RSP;
        break;

    case SDP_SVC_ATTR_REQ:
        rsp_len = sdp_attr_get(sdp, rsp, data, len);
        pdu_id = SDP_SVC_ATTR_RSP;
        break;

    case SDP_SVC_SEARCH_ATTR_REQ:
        rsp_len = sdp_svc_search_attr_get(sdp, rsp, data, len);
        pdu_id = SDP_SVC_SEARCH_ATTR_RSP;
        break;

    case SDP_ERROR_RSP:
    case SDP_SVC_ATTR_RSP:
    case SDP_SVC_SEARCH_RSP:
    case SDP_SVC_SEARCH_ATTR_RSP:
    default:
        fprintf(stderr, "%s: unexpected SDP PDU ID %02x.\n",
                        __FUNCTION__, pdu_id);
        err = SDP_INVALID_SYNTAX;
        break;
    }

    if (rsp_len < 0) {
        err = -rsp_len;
        rsp_len = 0;
    }

respond:
    if (err) {
        pdu_id = SDP_ERROR_RSP;
        rsp[rsp_len ++] = err >> 8;
        rsp[rsp_len ++] = err & 0xff;
    }

    sdu_out = sdp->channel->sdu_out(sdp->channel, rsp_len + PDU_HEADER_SIZE);

    sdu_out[0] = pdu_id;
    sdu_out[1] = transaction_id >> 8;
    sdu_out[2] = transaction_id & 0xff;
    sdu_out[3] = rsp_len >> 8;
    sdu_out[4] = rsp_len & 0xff;
    memcpy(sdu_out + PDU_HEADER_SIZE, rsp, rsp_len);

    sdp->channel->sdu_submit(sdp->channel);
}

static void bt_l2cap_sdp_close_ch(void *opaque)
{
    struct bt_l2cap_sdp_state_s *sdp = opaque;
    int i;

    for (i = 0; i < sdp->services; i ++) {
        qemu_free(sdp->service_list[i].attribute_list->pair);
        qemu_free(sdp->service_list[i].attribute_list);
        qemu_free(sdp->service_list[i].uuid);
    }
    qemu_free(sdp->service_list);
    qemu_free(sdp);
}

struct sdp_def_service_s {
    uint16_t class_uuid;
    struct sdp_def_attribute_s {
        uint16_t id;
        struct sdp_def_data_element_s {
            uint8_t type;
            union {
                uint32_t uint;
                const char *str;
                struct sdp_def_data_element_s *list;
            } value;
        } data;
    } attributes[];
};

/* Calculate a safe byte count to allocate that will store the given
 * element, at the same time count elements of a UUID type.  */
static int sdp_attr_max_size(struct sdp_def_data_element_s *element,
                int *uuids)
{
    int type = element->type & ~SDP_DSIZE_MASK;
    int len;

    if (type == SDP_DTYPE_UINT || type == SDP_DTYPE_UUID ||
                    type == SDP_DTYPE_BOOL) {
        if (type == SDP_DTYPE_UUID)
            (*uuids) ++;
        return 1 + (1 << (element->type & SDP_DSIZE_MASK));
    }

    if (type == SDP_DTYPE_STRING || type == SDP_DTYPE_URL) {
        if (element->type & SDP_DSIZE_MASK) {
            for (len = 0; element->value.str[len] |
                            element->value.str[len + 1]; len ++);
            return len;
        } else
            return 2 + strlen(element->value.str);
    }

    if (type != SDP_DTYPE_SEQ)
        exit(-1);
    len = 2;
    element = element->value.list;
    while (element->type)
        len += sdp_attr_max_size(element ++, uuids);
    if (len > 255)
        exit (-1);

    return len;
}

static int sdp_attr_write(uint8_t *data,
                struct sdp_def_data_element_s *element, int **uuid)
{
    int type = element->type & ~SDP_DSIZE_MASK;
    int len = 0;

    if (type == SDP_DTYPE_UINT || type == SDP_DTYPE_BOOL) {
        data[len ++] = element->type;
        if ((element->type & SDP_DSIZE_MASK) == SDP_DSIZE_1)
            data[len ++] = (element->value.uint >>  0) & 0xff;
        else if ((element->type & SDP_DSIZE_MASK) == SDP_DSIZE_2) {
            data[len ++] = (element->value.uint >>  8) & 0xff;
            data[len ++] = (element->value.uint >>  0) & 0xff;
        } else if ((element->type & SDP_DSIZE_MASK) == SDP_DSIZE_4) {
            data[len ++] = (element->value.uint >>  24) & 0xff;
            data[len ++] = (element->value.uint >>  16) & 0xff;
            data[len ++] = (element->value.uint >>  8) & 0xff;
            data[len ++] = (element->value.uint >>  0) & 0xff;
        }

        return len;
    }

    if (type == SDP_DTYPE_UUID) {
        *(*uuid) ++ = element->value.uint;

        data[len ++] = element->type;
        data[len ++] = (element->value.uint >>  24) & 0xff;
        data[len ++] = (element->value.uint >>  16) & 0xff;
        data[len ++] = (element->value.uint >>  8) & 0xff;
        data[len ++] = (element->value.uint >>  0) & 0xff;
        memcpy(data + len, bt_base_uuid, 12);

        return len + 12;
    }

    data[0] = type | SDP_DSIZE_NEXT1;
    if (type == SDP_DTYPE_STRING || type == SDP_DTYPE_URL) {
        if (element->type & SDP_DSIZE_MASK)
            for (len = 0; element->value.str[len] |
                            element->value.str[len + 1]; len ++);
        else
            len = strlen(element->value.str);
        memcpy(data + 2, element->value.str, data[1] = len);

        return len + 2;
    }

    len = 2;
    element = element->value.list;
    while (element->type)
        len += sdp_attr_write(data + len, element ++, uuid);
    data[1] = len - 2;

    return len;
}

static int sdp_attributeid_compare(const struct sdp_service_attribute_s *a,
                const struct sdp_service_attribute_s *b)
{
    return (int) b->attribute_id - a->attribute_id;
}

static int sdp_uuid_compare(const int *a, const int *b)
{
    return *a - *b;
}

static void sdp_service_record_build(struct sdp_service_record_s *record,
                struct sdp_def_service_s *def, int handle)
{
    int len = 0;
    uint8_t *data;
    int *uuid;

    record->uuids = 0;
    while (def->attributes[record->attributes].data.type) {
        len += 3;
        len += sdp_attr_max_size(&def->attributes[record->attributes ++].data,
                        &record->uuids);
    }
    record->uuids = 1 << ffs(record->uuids - 1);
    record->attribute_list =
            qemu_mallocz(record->attributes * sizeof(*record->attribute_list));
    record->uuid =
            qemu_mallocz(record->uuids * sizeof(*record->uuid));
    data = qemu_malloc(len);

    record->attributes = 0;
    uuid = record->uuid;
    while (def->attributes[record->attributes].data.type) {
        record->attribute_list[record->attributes].pair = data;

        len = 0;
        data[len ++] = SDP_DTYPE_UINT | SDP_DSIZE_2;
        data[len ++] = def->attributes[record->attributes].id >> 8;
        data[len ++] = def->attributes[record->attributes].id & 0xff;
        len += sdp_attr_write(data + len,
                        &def->attributes[record->attributes].data, &uuid);

        /* Special case: assign a ServiceRecordHandle in sequence */
        if (def->attributes[record->attributes].id == SDP_ATTR_RECORD_HANDLE)
            def->attributes[record->attributes].data.value.uint = handle;
        /* Note: we could also assign a ServiceDescription based on
         * sdp->device.device->lmp_name.  */

        record->attribute_list[record->attributes ++].len = len;
        data += len;
    }

    /* Sort the attribute list by the AttributeID */
    qsort(record->attribute_list, record->attributes,
                    sizeof(*record->attribute_list),
                    (void *) sdp_attributeid_compare);
    /* Sort the searchable UUIDs list for bisection */
    qsort(record->uuid, record->uuids,
                    sizeof(*record->uuid),
                    (void *) sdp_uuid_compare);
}

static void sdp_service_db_build(struct bt_l2cap_sdp_state_s *sdp,
                struct sdp_def_service_s **service)
{
    sdp->services = 0;
    while (service[sdp->services])
        sdp->services ++;
    sdp->service_list =
            qemu_mallocz(sdp->services * sizeof(*sdp->service_list));

    sdp->services = 0;
    while (*service) {
        sdp_service_record_build(&sdp->service_list[sdp->services],
                        *service, sdp->services);
        service ++;
        sdp->services ++;
    }
}

#define LAST { .type = 0 }
#define SERVICE(name, attrs)				\
    static struct sdp_def_service_s glue(glue(sdp_service_, name), _s) = { \
        .attributes = { attrs { .data = LAST } },	\
    };
#define ATTRIBUTE(attrid, val)	{ .id = glue(SDP_ATTR_, attrid), .data = val },
#define UINT8(val)	{				\
        .type       = SDP_DTYPE_UINT | SDP_DSIZE_1,	\
        .value.uint = val,				\
    },
#define UINT16(val)	{				\
        .type       = SDP_DTYPE_UINT | SDP_DSIZE_2,	\
        .value.uint = val,				\
    },
#define UINT32(val)	{				\
        .type       = SDP_DTYPE_UINT | SDP_DSIZE_4,	\
        .value.uint = val,				\
    },
#define UUID128(val)	{				\
        .type       = SDP_DTYPE_UUID | SDP_DSIZE_16,	\
        .value.uint = val,				\
    },
#define TRUE	{				\
        .type       = SDP_DTYPE_BOOL | SDP_DSIZE_1,	\
        .value.uint = 1,				\
    },
#define FALSE	{				\
        .type       = SDP_DTYPE_BOOL | SDP_DSIZE_1,	\
        .value.uint = 0,				\
    },
#define STRING(val)	{				\
        .type       = SDP_DTYPE_STRING,			\
        .value.str  = val,				\
    },
#define ARRAY(...)	{				\
        .type       = SDP_DTYPE_STRING | SDP_DSIZE_2,	\
        .value.str  = (char []) { __VA_ARGS__, 0, 0 },	\
    },
#define URL(val)	{				\
        .type       = SDP_DTYPE_URL,			\
        .value.str  = val,				\
    },
#if 1
#define LIST(val)	{				\
        .type       = SDP_DTYPE_SEQ,			\
        .value.list = (struct sdp_def_data_element_s []) { val LAST }, \
    },
#endif

/* Try to keep each single attribute below MAX_PDU_OUT_SIZE bytes
 * in resulting SDP data representation size.  */

SERVICE(hid,
    ATTRIBUTE(RECORD_HANDLE,   UINT32(0))	/* Filled in later */
    ATTRIBUTE(SVCLASS_ID_LIST, LIST(UUID128(HID_SVCLASS_ID)))
    ATTRIBUTE(RECORD_STATE,    UINT32(1))
    ATTRIBUTE(PROTO_DESC_LIST, LIST(
        LIST(UUID128(L2CAP_UUID) UINT16(BT_PSM_HID_CTRL))
        LIST(UUID128(HIDP_UUID))
    ))
    ATTRIBUTE(BROWSE_GRP_LIST, LIST(UUID128(0x1002)))
    ATTRIBUTE(LANG_BASE_ATTR_ID_LIST, LIST(
        UINT16(0x656e) UINT16(0x006a) UINT16(0x0100)
    ))
    ATTRIBUTE(PFILE_DESC_LIST, LIST(
        LIST(UUID128(HID_PROFILE_ID) UINT16(0x0100))
    ))
    ATTRIBUTE(DOC_URL,         URL("http://bellard.org/qemu/user-doc.html"))
    ATTRIBUTE(SVCNAME_PRIMARY, STRING("QEMU Bluetooth HID"))
    ATTRIBUTE(SVCDESC_PRIMARY, STRING("QEMU Keyboard/Mouse"))
    ATTRIBUTE(SVCPROV_PRIMARY, STRING("QEMU " QEMU_VERSION))

    /* Profile specific */
    ATTRIBUTE(DEVICE_RELEASE_NUMBER,	UINT16(0x0091)) /* Deprecated, remove */
    ATTRIBUTE(PARSER_VERSION,		UINT16(0x0111))
    /* TODO: extract from l2cap_device->device.class[0] */
    ATTRIBUTE(DEVICE_SUBCLASS,		UINT8(0x40))
    ATTRIBUTE(COUNTRY_CODE,		UINT8(0x15))
    ATTRIBUTE(VIRTUAL_CABLE,		TRUE)
    ATTRIBUTE(RECONNECT_INITIATE,	FALSE)
    /* TODO: extract from hid->usbdev->report_desc */
    ATTRIBUTE(DESCRIPTOR_LIST,		LIST(
        LIST(UINT8(0x22) ARRAY(
            0x05, 0x01,	/* Usage Page (Generic Desktop) */
            0x09, 0x06,	/* Usage (Keyboard) */
            0xa1, 0x01,	/* Collection (Application) */
            0x75, 0x01,	/*   Report Size (1) */
            0x95, 0x08,	/*   Report Count (8) */
            0x05, 0x07,	/*   Usage Page (Key Codes) */
            0x19, 0xe0,	/*   Usage Minimum (224) */
            0x29, 0xe7,	/*   Usage Maximum (231) */
            0x15, 0x00,	/*   Logical Minimum (0) */
            0x25, 0x01,	/*   Logical Maximum (1) */
            0x81, 0x02,	/*   Input (Data, Variable, Absolute) */
            0x95, 0x01,	/*   Report Count (1) */
            0x75, 0x08,	/*   Report Size (8) */
            0x81, 0x01,	/*   Input (Constant) */
            0x95, 0x05,	/*   Report Count (5) */
            0x75, 0x01,	/*   Report Size (1) */
            0x05, 0x08,	/*   Usage Page (LEDs) */
            0x19, 0x01,	/*   Usage Minimum (1) */
            0x29, 0x05,	/*   Usage Maximum (5) */
            0x91, 0x02,	/*   Output (Data, Variable, Absolute) */
            0x95, 0x01,	/*   Report Count (1) */
            0x75, 0x03,	/*   Report Size (3) */
            0x91, 0x01,	/*   Output (Constant) */
            0x95, 0x06,	/*   Report Count (6) */
            0x75, 0x08,	/*   Report Size (8) */
            0x15, 0x00,	/*   Logical Minimum (0) */
            0x25, 0xff,	/*   Logical Maximum (255) */
            0x05, 0x07,	/*   Usage Page (Key Codes) */
            0x19, 0x00,	/*   Usage Minimum (0) */
            0x29, 0xff,	/*   Usage Maximum (255) */
            0x81, 0x00,	/*   Input (Data, Array) */
            0xc0	/* End Collection */
    ))))
    ATTRIBUTE(LANG_ID_BASE_LIST,	LIST(
        LIST(UINT16(0x0409) UINT16(0x0100))
    ))
    ATTRIBUTE(SDP_DISABLE,		FALSE)
    ATTRIBUTE(BATTERY_POWER,		TRUE)
    ATTRIBUTE(REMOTE_WAKEUP,		TRUE)
    ATTRIBUTE(BOOT_DEVICE,		TRUE)	/* XXX: untested */
    ATTRIBUTE(SUPERVISION_TIMEOUT,	UINT16(0x0c80))
    ATTRIBUTE(NORMALLY_CONNECTABLE,	TRUE)
    ATTRIBUTE(PROFILE_VERSION,		UINT16(0x0100))
)

SERVICE(sdp,
    ATTRIBUTE(RECORD_HANDLE,   UINT32(0))	/* Filled in later */
    ATTRIBUTE(SVCLASS_ID_LIST, LIST(UUID128(SDP_SERVER_SVCLASS_ID)))
    ATTRIBUTE(RECORD_STATE,    UINT32(1))
    ATTRIBUTE(PROTO_DESC_LIST, LIST(
        LIST(UUID128(L2CAP_UUID) UINT16(BT_PSM_SDP))
        LIST(UUID128(SDP_UUID))
    ))
    ATTRIBUTE(BROWSE_GRP_LIST, LIST(UUID128(0x1002)))
    ATTRIBUTE(LANG_BASE_ATTR_ID_LIST, LIST(
        UINT16(0x656e) UINT16(0x006a) UINT16(0x0100)
    ))
    ATTRIBUTE(PFILE_DESC_LIST, LIST(
        LIST(UUID128(SDP_SERVER_PROFILE_ID) UINT16(0x0100))
    ))
    ATTRIBUTE(DOC_URL,         URL("http://bellard.org/qemu/user-doc.html"))
    ATTRIBUTE(SVCPROV_PRIMARY, STRING("QEMU " QEMU_VERSION))

    /* Profile specific */
    ATTRIBUTE(VERSION_NUM_LIST, LIST(UINT16(0x0100)))
    ATTRIBUTE(SVCDB_STATE    , UINT32(1))
)

SERVICE(pnp,
    ATTRIBUTE(RECORD_HANDLE,   UINT32(0))	/* Filled in later */
    ATTRIBUTE(SVCLASS_ID_LIST, LIST(UUID128(PNP_INFO_SVCLASS_ID)))
    ATTRIBUTE(RECORD_STATE,    UINT32(1))
    ATTRIBUTE(PROTO_DESC_LIST, LIST(
        LIST(UUID128(L2CAP_UUID) UINT16(BT_PSM_SDP))
        LIST(UUID128(SDP_UUID))
    ))
    ATTRIBUTE(BROWSE_GRP_LIST, LIST(UUID128(0x1002)))
    ATTRIBUTE(LANG_BASE_ATTR_ID_LIST, LIST(
        UINT16(0x656e) UINT16(0x006a) UINT16(0x0100)
    ))
    ATTRIBUTE(PFILE_DESC_LIST, LIST(
        LIST(UUID128(PNP_INFO_PROFILE_ID) UINT16(0x0100))
    ))
    ATTRIBUTE(DOC_URL,         URL("http://bellard.org/qemu/user-doc.html"))
    ATTRIBUTE(SVCPROV_PRIMARY, STRING("QEMU " QEMU_VERSION))

    /* Profile specific */
    ATTRIBUTE(SPECIFICATION_ID, UINT16(0x0100))
    ATTRIBUTE(VERSION,         UINT16(0x0100))
    ATTRIBUTE(PRIMARY_RECORD,  TRUE)
)

static int bt_l2cap_sdp_new_ch(struct bt_l2cap_device_s *dev,
                struct bt_l2cap_conn_params_s *params)
{
    struct bt_l2cap_sdp_state_s *sdp = qemu_mallocz(sizeof(*sdp));
    struct sdp_def_service_s *services[] = {
        &sdp_service_sdp_s,
        &sdp_service_hid_s,
        &sdp_service_pnp_s,
        NULL,
    };

    sdp->channel = params;
    sdp->channel->opaque = sdp;
    sdp->channel->close = bt_l2cap_sdp_close_ch;
    sdp->channel->sdu_in = bt_l2cap_sdp_sdu_in;

    sdp_service_db_build(sdp, services);

    return 0;
}

void bt_l2cap_sdp_init(struct bt_l2cap_device_s *dev)
{
    bt_l2cap_psm_register(dev, BT_PSM_SDP,
                    MAX_PDU_OUT_SIZE, bt_l2cap_sdp_new_ch);
}
