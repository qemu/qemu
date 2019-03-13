/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef STREAM_H_
#define STREAM_H_

#include "libslirp.h"

typedef struct SlirpIStream {
    SlirpReadCb read_cb;
    void *opaque;
} SlirpIStream;

typedef struct SlirpOStream {
    SlirpWriteCb write_cb;
    void *opaque;
} SlirpOStream;

bool slirp_istream_read(SlirpIStream *f, void *buf, size_t size);
bool slirp_ostream_write(SlirpOStream *f, const void *buf, size_t size);

uint8_t slirp_istream_read_u8(SlirpIStream *f);
bool slirp_ostream_write_u8(SlirpOStream *f, uint8_t b);

uint16_t slirp_istream_read_u16(SlirpIStream *f);
bool slirp_ostream_write_u16(SlirpOStream *f, uint16_t b);

uint32_t slirp_istream_read_u32(SlirpIStream *f);
bool slirp_ostream_write_u32(SlirpOStream *f, uint32_t b);

int16_t slirp_istream_read_i16(SlirpIStream *f);
bool slirp_ostream_write_i16(SlirpOStream *f, int16_t b);

int32_t slirp_istream_read_i32(SlirpIStream *f);
bool slirp_ostream_write_i32(SlirpOStream *f, int32_t b);

#endif /* STREAM_H_ */
