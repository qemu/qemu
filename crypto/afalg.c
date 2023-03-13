/*
 * QEMU Crypto af_alg support
 *
 * Copyright (c) 2017 HUAWEI TECHNOLOGIES CO., LTD.
 *
 * Authors:
 *    Longpeng(Mike) <longpeng2@huawei.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */
#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/sockets.h"
#include "qapi/error.h"
#include "afalgpriv.h"

static bool
qcrypto_afalg_build_saddr(const char *type, const char *name,
                          struct sockaddr_alg *salg, Error **errp)
{
    salg->salg_family = AF_ALG;

    if (strnlen(type, SALG_TYPE_LEN_MAX) >= SALG_TYPE_LEN_MAX) {
        error_setg(errp, "Afalg type(%s) is larger than %d bytes",
                   type, SALG_TYPE_LEN_MAX);
        return false;
    }

    if (strnlen(name, SALG_NAME_LEN_MAX) >= SALG_NAME_LEN_MAX) {
        error_setg(errp, "Afalg name(%s) is larger than %d bytes",
                   name, SALG_NAME_LEN_MAX);
        return false;
    }

    pstrcpy((char *)salg->salg_type, SALG_TYPE_LEN_MAX, type);
    pstrcpy((char *)salg->salg_name, SALG_NAME_LEN_MAX, name);

    return true;
}

static int
qcrypto_afalg_socket_bind(const char *type, const char *name,
                          Error **errp)
{
    int sbind;
    struct sockaddr_alg salg = {0};

    if (!qcrypto_afalg_build_saddr(type, name, &salg, errp)) {
        return -1;
    }

    sbind = qemu_socket(AF_ALG, SOCK_SEQPACKET, 0);
    if (sbind < 0) {
        error_setg_errno(errp, errno, "Failed to create socket");
        return -1;
    }

    if (bind(sbind, (const struct sockaddr *)&salg, sizeof(salg)) != 0) {
        error_setg_errno(errp, errno, "Failed to bind socket");
        close(sbind);
        return -1;
    }

    return sbind;
}

QCryptoAFAlg *
qcrypto_afalg_comm_alloc(const char *type, const char *name,
                         Error **errp)
{
    QCryptoAFAlg *afalg;

    afalg = g_new0(QCryptoAFAlg, 1);
    /* initilize crypto API socket */
    afalg->opfd = -1;
    afalg->tfmfd = qcrypto_afalg_socket_bind(type, name, errp);
    if (afalg->tfmfd == -1) {
        goto error;
    }

    afalg->opfd = qemu_accept(afalg->tfmfd, NULL, 0);
    if (afalg->opfd == -1) {
        error_setg_errno(errp, errno, "Failed to accept socket");
        goto error;
    }

    return afalg;

error:
    qcrypto_afalg_comm_free(afalg);
    return NULL;
}

void qcrypto_afalg_comm_free(QCryptoAFAlg *afalg)
{
    if (!afalg) {
        return;
    }

    if (afalg->msg) {
        g_free(afalg->msg->msg_control);
        g_free(afalg->msg);
    }

    if (afalg->tfmfd != -1) {
        close(afalg->tfmfd);
    }

    if (afalg->opfd != -1) {
        close(afalg->opfd);
    }

    g_free(afalg);
}
