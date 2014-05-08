#ifndef NBD_CLIENT_H
#define NBD_CLIENT_H

#include "qemu-common.h"
#include "block/nbd.h"
#include "block/block_int.h"

/* #define DEBUG_NBD */

#if defined(DEBUG_NBD)
#define logout(fmt, ...) \
    fprintf(stderr, "nbd\t%-24s" fmt, __func__, ##__VA_ARGS__)
#else
#define logout(fmt, ...) ((void)0)
#endif

#define MAX_NBD_REQUESTS    16

typedef struct NbdClientSession {
    int sock;
    uint32_t nbdflags;
    off_t size;
    size_t blocksize;

    CoMutex send_mutex;
    CoMutex free_sema;
    Coroutine *send_coroutine;
    int in_flight;

    Coroutine *recv_coroutine[MAX_NBD_REQUESTS];
    struct nbd_reply reply;

    bool is_unix;

    BlockDriverState *bs;
} NbdClientSession;

int nbd_client_session_init(NbdClientSession *client, BlockDriverState *bs,
                            int sock, const char *export_name);
void nbd_client_session_close(NbdClientSession *client);

int nbd_client_session_co_discard(NbdClientSession *client, int64_t sector_num,
                                  int nb_sectors);
int nbd_client_session_co_flush(NbdClientSession *client);
int nbd_client_session_co_writev(NbdClientSession *client, int64_t sector_num,
                                 int nb_sectors, QEMUIOVector *qiov);
int nbd_client_session_co_readv(NbdClientSession *client, int64_t sector_num,
                                int nb_sectors, QEMUIOVector *qiov);

void nbd_client_session_detach_aio_context(NbdClientSession *client);
void nbd_client_session_attach_aio_context(NbdClientSession *client,
                                           AioContext *new_context);

#endif /* NBD_CLIENT_H */
