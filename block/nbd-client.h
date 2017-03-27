#ifndef NBD_CLIENT_H
#define NBD_CLIENT_H

#include "qemu-common.h"
#include "block/nbd.h"
#include "block/block_int.h"
#include "io/channel-socket.h"

/* #define DEBUG_NBD */

#if defined(DEBUG_NBD)
#define logout(fmt, ...) \
    fprintf(stderr, "nbd\t%-24s" fmt, __func__, ##__VA_ARGS__)
#else
#define logout(fmt, ...) ((void)0)
#endif

#define MAX_NBD_REQUESTS    16

typedef struct NBDClientSession {
    QIOChannelSocket *sioc; /* The master data channel */
    QIOChannel *ioc; /* The current I/O channel which may differ (eg TLS) */
    uint16_t nbdflags;
    off_t size;

    CoMutex send_mutex;
    CoQueue free_sema;
    Coroutine *read_reply_co;
    int in_flight;

    Coroutine *recv_coroutine[MAX_NBD_REQUESTS];
    NBDReply reply;
} NBDClientSession;

NBDClientSession *nbd_get_client_session(BlockDriverState *bs);

int nbd_client_init(BlockDriverState *bs,
                    QIOChannelSocket *sock,
                    const char *export_name,
                    QCryptoTLSCreds *tlscreds,
                    const char *hostname,
                    Error **errp);
void nbd_client_close(BlockDriverState *bs);

int nbd_client_co_pdiscard(BlockDriverState *bs, int64_t offset, int count);
int nbd_client_co_flush(BlockDriverState *bs);
int nbd_client_co_pwritev(BlockDriverState *bs, uint64_t offset,
                          uint64_t bytes, QEMUIOVector *qiov, int flags);
int nbd_client_co_pwrite_zeroes(BlockDriverState *bs, int64_t offset,
                                int count, BdrvRequestFlags flags);
int nbd_client_co_preadv(BlockDriverState *bs, uint64_t offset,
                         uint64_t bytes, QEMUIOVector *qiov, int flags);

void nbd_client_detach_aio_context(BlockDriverState *bs);
void nbd_client_attach_aio_context(BlockDriverState *bs,
                                   AioContext *new_context);

#endif /* NBD_CLIENT_H */
