/*
 * QEMU rocker switch emulation - TLV parsing and composing
 *
 * Copyright (c) 2014 Jiri Pirko <jiri@resnulli.us>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef ROCKER_TLV_H
#define ROCKER_TLV_H

#define ROCKER_TLV_ALIGNTO 8U
#define ROCKER_TLV_ALIGN(len) \
    (((len) + ROCKER_TLV_ALIGNTO - 1) & ~(ROCKER_TLV_ALIGNTO - 1))
#define ROCKER_TLV_HDRLEN ROCKER_TLV_ALIGN(sizeof(RockerTlv))

/*
 *  <------- ROCKER_TLV_HDRLEN -------> <--- ROCKER_TLV_ALIGN(payload) --->
 * +-----------------------------+- - -+- - - - - - - - - - - - - - -+- - -+
 * |             Header          | Pad |           Payload           | Pad |
 * |          (RockerTlv)        | ing |                             | ing |
 * +-----------------------------+- - -+- - - - - - - - - - - - - - -+- - -+
 *  <--------------------------- tlv->len -------------------------->
 */

static inline RockerTlv *rocker_tlv_next(const RockerTlv *tlv, int *remaining)
{
    int totlen = ROCKER_TLV_ALIGN(le16_to_cpu(tlv->len));

    *remaining -= totlen;
    return (RockerTlv *) ((char *) tlv + totlen);
}

static inline int rocker_tlv_ok(const RockerTlv *tlv, int remaining)
{
    return remaining >= (int) ROCKER_TLV_HDRLEN &&
           le16_to_cpu(tlv->len) >= ROCKER_TLV_HDRLEN &&
           le16_to_cpu(tlv->len) <= remaining;
}

#define rocker_tlv_for_each(pos, head, len, rem) \
    for (pos = head, rem = len; \
         rocker_tlv_ok(pos, rem); \
         pos = rocker_tlv_next(pos, &(rem)))

#define rocker_tlv_for_each_nested(pos, tlv, rem) \
        rocker_tlv_for_each(pos, rocker_tlv_data(tlv), rocker_tlv_len(tlv), rem)

static inline int rocker_tlv_size(int payload)
{
    return ROCKER_TLV_HDRLEN + payload;
}

static inline int rocker_tlv_total_size(int payload)
{
    return ROCKER_TLV_ALIGN(rocker_tlv_size(payload));
}

static inline int rocker_tlv_padlen(int payload)
{
    return rocker_tlv_total_size(payload) - rocker_tlv_size(payload);
}

static inline int rocker_tlv_type(const RockerTlv *tlv)
{
    return le32_to_cpu(tlv->type);
}

static inline void *rocker_tlv_data(const RockerTlv *tlv)
{
    return (char *) tlv + ROCKER_TLV_HDRLEN;
}

static inline int rocker_tlv_len(const RockerTlv *tlv)
{
    return le16_to_cpu(tlv->len) - ROCKER_TLV_HDRLEN;
}

static inline uint8_t rocker_tlv_get_u8(const RockerTlv *tlv)
{
    return *(uint8_t *) rocker_tlv_data(tlv);
}

static inline uint16_t rocker_tlv_get_u16(const RockerTlv *tlv)
{
    return *(uint16_t *) rocker_tlv_data(tlv);
}

static inline uint32_t rocker_tlv_get_u32(const RockerTlv *tlv)
{
    return *(uint32_t *) rocker_tlv_data(tlv);
}

static inline uint64_t rocker_tlv_get_u64(const RockerTlv *tlv)
{
    return *(uint64_t *) rocker_tlv_data(tlv);
}

static inline uint16_t rocker_tlv_get_le16(const RockerTlv *tlv)
{
    return lduw_le_p(rocker_tlv_data(tlv));
}

static inline uint32_t rocker_tlv_get_le32(const RockerTlv *tlv)
{
    return ldl_le_p(rocker_tlv_data(tlv));
}

static inline uint64_t rocker_tlv_get_le64(const RockerTlv *tlv)
{
    return ldq_le_p(rocker_tlv_data(tlv));
}

static inline void rocker_tlv_parse(RockerTlv **tb, int maxtype,
                                    const char *buf, int buf_len)
{
    const RockerTlv *tlv;
    const RockerTlv *head = (const RockerTlv *) buf;
    int rem;

    memset(tb, 0, sizeof(RockerTlv *) * (maxtype + 1));

    rocker_tlv_for_each(tlv, head, buf_len, rem) {
        uint32_t type = rocker_tlv_type(tlv);

        if (type > 0 && type <= maxtype) {
            tb[type] = (RockerTlv *) tlv;
        }
    }
}

static inline void rocker_tlv_parse_nested(RockerTlv **tb, int maxtype,
                                           const RockerTlv *tlv)
{
    rocker_tlv_parse(tb, maxtype, rocker_tlv_data(tlv), rocker_tlv_len(tlv));
}

static inline RockerTlv *rocker_tlv_start(char *buf, int buf_pos)
{
    return (RockerTlv *) (buf + buf_pos);
}

static inline void rocker_tlv_put_iov(char *buf, int *buf_pos,
                                      int type, const struct iovec *iov,
                                      const unsigned int iovcnt)
{
    size_t len = iov_size(iov, iovcnt);
    int total_size = rocker_tlv_total_size(len);
    RockerTlv *tlv;

    tlv = rocker_tlv_start(buf, *buf_pos);
    *buf_pos += total_size;
    tlv->type = cpu_to_le32(type);
    tlv->len = cpu_to_le16(rocker_tlv_size(len));
    iov_to_buf(iov, iovcnt, 0, rocker_tlv_data(tlv), len);
    memset((char *) tlv + le16_to_cpu(tlv->len), 0, rocker_tlv_padlen(len));
}

static inline void rocker_tlv_put(char *buf, int *buf_pos,
                                  int type, int len, void *data)
{
    struct iovec iov = {
        .iov_base = data,
        .iov_len = len,
    };

    rocker_tlv_put_iov(buf, buf_pos, type, &iov, 1);
}

static inline void rocker_tlv_put_u8(char *buf, int *buf_pos,
                                     int type, uint8_t value)
{
    rocker_tlv_put(buf, buf_pos, type, sizeof(uint8_t), &value);
}

static inline void rocker_tlv_put_u16(char *buf, int *buf_pos,
                                      int type, uint16_t value)
{
    rocker_tlv_put(buf, buf_pos, type, sizeof(uint16_t), &value);
}

static inline void rocker_tlv_put_u32(char *buf, int *buf_pos,
                                      int type, uint32_t value)
{
    rocker_tlv_put(buf, buf_pos, type, sizeof(uint32_t), &value);
}

static inline void rocker_tlv_put_u64(char *buf, int *buf_pos,
                                      int type, uint64_t value)
{
    rocker_tlv_put(buf, buf_pos, type, sizeof(uint64_t), &value);
}

static inline void rocker_tlv_put_le16(char *buf, int *buf_pos,
                                       int type, uint16_t value)
{
    value = cpu_to_le16(value);
    rocker_tlv_put(buf, buf_pos, type, sizeof(uint16_t), &value);
}

static inline void rocker_tlv_put_le32(char *buf, int *buf_pos,
                                       int type, uint32_t value)
{
    value = cpu_to_le32(value);
    rocker_tlv_put(buf, buf_pos, type, sizeof(uint32_t), &value);
}

static inline void rocker_tlv_put_le64(char *buf, int *buf_pos,
                                       int type, uint64_t value)
{
    value = cpu_to_le64(value);
    rocker_tlv_put(buf, buf_pos, type, sizeof(uint64_t), &value);
}

static inline RockerTlv *rocker_tlv_nest_start(char *buf, int *buf_pos,
                                               int type)
{
    RockerTlv *start = rocker_tlv_start(buf, *buf_pos);

    rocker_tlv_put(buf, buf_pos, type, 0, NULL);
    return start;
}

static inline void rocker_tlv_nest_end(char *buf, int *buf_pos,
                                       RockerTlv *start)
{
    start->len = (char *) rocker_tlv_start(buf, *buf_pos) - (char *) start;
}

static inline void rocker_tlv_nest_cancel(char *buf, int *buf_pos,
                                          RockerTlv *start)
{
    *buf_pos = (char *) start - buf;
}

#endif
