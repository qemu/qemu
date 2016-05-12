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

typedef struct NbdClientSession {
    QIOChannelSocket *sioc; /* The master data channel */
    QIOChannel *ioc; /* The current I/O channel which may differ (eg TLS) */
    uint32_t nbdflags;
    off_t size;

    CoMutex send_mutex;
    CoMutex free_sema;
    Coroutine *send_coroutine;
    int in_flight;

    Coroutine *recv_coroutine[MAX_NBD_REQUESTS];
    struct nbd_reply reply;

    bool is_unix;
} NbdClientSession;

NbdClientSession *nbd_get_client_session(BlockDriverState *bs);

int nbd_client_init(BlockDriverState *bs,
                    QIOChannelSocket *sock,
                    const char *export_name,
                    QCryptoTLSCreds *tlscreds,
                    const char *hostname,
                    Error **errp);
void nbd_client_close(BlockDriverState *bs);

int nbd_client_co_discard(BlockDriverState *bs, int64_t sector_num,
                          int nb_sectors);
int nbd_client_co_flush(BlockDriverState *bs);
int nbd_client_co_writev(BlockDriverState *bs, int64_t sector_num,
                         int nb_sectors, QEMUIOVector *qiov, int flags);
int nbd_client_co_readv(BlockDriverState *bs, int64_t sector_num,
                        int nb_sectors, QEMUIOVector *qiov);

void nbd_client_detach_aio_context(BlockDriverState *bs);
void nbd_client_attach_aio_context(BlockDriverState *bs,
                                   AioContext *new_context);

#endif /* NBD_CLIENT_H */
