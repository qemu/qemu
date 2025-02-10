/*
 * QEMU NVM Express End-to-End Data Protection support
 *
 * Copyright (c) 2021 Samsung Electronics Co., Ltd.
 *
 * Authors:
 *   Klaus Jensen           <k.jensen@samsung.com>
 *   Gollu Appalanaidu      <anaidu.gollu@samsung.com>
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "system/block-backend.h"

#include "nvme.h"
#include "dif.h"
#include "trace.h"

uint16_t nvme_check_prinfo(NvmeNamespace *ns, uint8_t prinfo, uint64_t slba,
                           uint64_t reftag)
{
    uint64_t mask = ns->pif ? 0xffffffffffff : 0xffffffff;

    if ((NVME_ID_NS_DPS_TYPE(ns->id_ns.dps) == NVME_ID_NS_DPS_TYPE_1) &&
        (prinfo & NVME_PRINFO_PRCHK_REF) && (slba & mask) != reftag) {
        return NVME_INVALID_PROT_INFO | NVME_DNR;
    }

    if ((NVME_ID_NS_DPS_TYPE(ns->id_ns.dps) == NVME_ID_NS_DPS_TYPE_3) &&
        (prinfo & NVME_PRINFO_PRCHK_REF)) {
        return NVME_INVALID_PROT_INFO;
    }

    return NVME_SUCCESS;
}

/* from Linux kernel (crypto/crct10dif_common.c) */
static uint16_t crc16_t10dif(uint16_t crc, const unsigned char *buffer,
                             size_t len)
{
    unsigned int i;

    for (i = 0; i < len; i++) {
        crc = (crc << 8) ^ crc16_t10dif_table[((crc >> 8) ^ buffer[i]) & 0xff];
    }

    return crc;
}

/* from Linux kernel (lib/crc64.c) */
static uint64_t crc64_nvme(uint64_t crc, const unsigned char *buffer,
                           size_t len)
{
    size_t i;

    for (i = 0; i < len; i++) {
        crc = (crc >> 8) ^ crc64_nvme_table[(crc & 0xff) ^ buffer[i]];
    }

    return crc ^ (uint64_t)~0;
}

static void nvme_dif_pract_generate_dif_crc16(NvmeNamespace *ns, uint8_t *buf,
                                              size_t len, uint8_t *mbuf,
                                              size_t mlen, uint16_t apptag,
                                              uint64_t *reftag)
{
    uint8_t *end = buf + len;
    int16_t pil = 0;

    if (!(ns->id_ns.dps & NVME_ID_NS_DPS_FIRST_EIGHT)) {
        pil = ns->lbaf.ms - nvme_pi_tuple_size(ns);
    }

    trace_pci_nvme_dif_pract_generate_dif_crc16(len, ns->lbasz,
                                                ns->lbasz + pil, apptag,
                                                *reftag);

    for (; buf < end; buf += ns->lbasz, mbuf += ns->lbaf.ms) {
        NvmeDifTuple *dif = (NvmeDifTuple *)(mbuf + pil);
        uint16_t crc = crc16_t10dif(0x0, buf, ns->lbasz);

        if (pil) {
            crc = crc16_t10dif(crc, mbuf, pil);
        }

        dif->g16.guard = cpu_to_be16(crc);
        dif->g16.apptag = cpu_to_be16(apptag);
        dif->g16.reftag = cpu_to_be32(*reftag);

        if (NVME_ID_NS_DPS_TYPE(ns->id_ns.dps) != NVME_ID_NS_DPS_TYPE_3) {
            (*reftag)++;
        }
    }
}

static void nvme_dif_pract_generate_dif_crc64(NvmeNamespace *ns, uint8_t *buf,
                                              size_t len, uint8_t *mbuf,
                                              size_t mlen, uint16_t apptag,
                                              uint64_t *reftag)
{
    uint8_t *end = buf + len;
    int16_t pil = 0;

    if (!(ns->id_ns.dps & NVME_ID_NS_DPS_FIRST_EIGHT)) {
        pil = ns->lbaf.ms - 16;
    }

    trace_pci_nvme_dif_pract_generate_dif_crc64(len, ns->lbasz,
                                                ns->lbasz + pil, apptag,
                                                *reftag);

    for (; buf < end; buf += ns->lbasz, mbuf += ns->lbaf.ms) {
        NvmeDifTuple *dif = (NvmeDifTuple *)(mbuf + pil);
        uint64_t crc = crc64_nvme(~0ULL, buf, ns->lbasz);

        if (pil) {
            crc = crc64_nvme(~crc, mbuf, pil);
        }

        dif->g64.guard = cpu_to_be64(crc);
        dif->g64.apptag = cpu_to_be16(apptag);

        dif->g64.sr[0] = *reftag >> 40;
        dif->g64.sr[1] = *reftag >> 32;
        dif->g64.sr[2] = *reftag >> 24;
        dif->g64.sr[3] = *reftag >> 16;
        dif->g64.sr[4] = *reftag >> 8;
        dif->g64.sr[5] = *reftag;

        if (NVME_ID_NS_DPS_TYPE(ns->id_ns.dps) != NVME_ID_NS_DPS_TYPE_3) {
            (*reftag)++;
        }
    }
}

void nvme_dif_pract_generate_dif(NvmeNamespace *ns, uint8_t *buf, size_t len,
                                 uint8_t *mbuf, size_t mlen, uint16_t apptag,
                                 uint64_t *reftag)
{
    switch (ns->pif) {
    case NVME_PI_GUARD_16:
        return nvme_dif_pract_generate_dif_crc16(ns, buf, len, mbuf, mlen,
                                                 apptag, reftag);
    case NVME_PI_GUARD_64:
        return nvme_dif_pract_generate_dif_crc64(ns, buf, len, mbuf, mlen,
                                                 apptag, reftag);
    }

    abort();
}

static uint16_t nvme_dif_prchk_crc16(NvmeNamespace *ns, NvmeDifTuple *dif,
                                     uint8_t *buf, uint8_t *mbuf, size_t pil,
                                     uint8_t prinfo, uint16_t apptag,
                                     uint16_t appmask, uint64_t reftag)
{
    switch (NVME_ID_NS_DPS_TYPE(ns->id_ns.dps)) {
    case NVME_ID_NS_DPS_TYPE_3:
        if (be32_to_cpu(dif->g16.reftag) != 0xffffffff) {
            break;
        }

        /* fallthrough */
    case NVME_ID_NS_DPS_TYPE_1:
    case NVME_ID_NS_DPS_TYPE_2:
        if (be16_to_cpu(dif->g16.apptag) != 0xffff) {
            break;
        }

        trace_pci_nvme_dif_prchk_disabled_crc16(be16_to_cpu(dif->g16.apptag),
                                                be32_to_cpu(dif->g16.reftag));

        return NVME_SUCCESS;
    }

    if (prinfo & NVME_PRINFO_PRCHK_GUARD) {
        uint16_t crc = crc16_t10dif(0x0, buf, ns->lbasz);

        if (pil) {
            crc = crc16_t10dif(crc, mbuf, pil);
        }

        trace_pci_nvme_dif_prchk_guard_crc16(be16_to_cpu(dif->g16.guard), crc);

        if (be16_to_cpu(dif->g16.guard) != crc) {
            return NVME_E2E_GUARD_ERROR;
        }
    }

    if (prinfo & NVME_PRINFO_PRCHK_APP) {
        trace_pci_nvme_dif_prchk_apptag(be16_to_cpu(dif->g16.apptag), apptag,
                                        appmask);

        if ((be16_to_cpu(dif->g16.apptag) & appmask) != (apptag & appmask)) {
            return NVME_E2E_APP_ERROR;
        }
    }

    if (prinfo & NVME_PRINFO_PRCHK_REF) {
        trace_pci_nvme_dif_prchk_reftag_crc16(be32_to_cpu(dif->g16.reftag),
                                              reftag);

        if (be32_to_cpu(dif->g16.reftag) != reftag) {
            return NVME_E2E_REF_ERROR;
        }
    }

    return NVME_SUCCESS;
}

static uint16_t nvme_dif_prchk_crc64(NvmeNamespace *ns, NvmeDifTuple *dif,
                                     uint8_t *buf, uint8_t *mbuf, size_t pil,
                                     uint8_t prinfo, uint16_t apptag,
                                     uint16_t appmask, uint64_t reftag)
{
    uint64_t r = 0;

    r |= (uint64_t)dif->g64.sr[0] << 40;
    r |= (uint64_t)dif->g64.sr[1] << 32;
    r |= (uint64_t)dif->g64.sr[2] << 24;
    r |= (uint64_t)dif->g64.sr[3] << 16;
    r |= (uint64_t)dif->g64.sr[4] << 8;
    r |= (uint64_t)dif->g64.sr[5];

    switch (NVME_ID_NS_DPS_TYPE(ns->id_ns.dps)) {
    case NVME_ID_NS_DPS_TYPE_3:
        if (r != 0xffffffffffff) {
            break;
        }

        /* fallthrough */
    case NVME_ID_NS_DPS_TYPE_1:
    case NVME_ID_NS_DPS_TYPE_2:
        if (be16_to_cpu(dif->g64.apptag) != 0xffff) {
            break;
        }

        trace_pci_nvme_dif_prchk_disabled_crc64(be16_to_cpu(dif->g16.apptag),
                                                r);

        return NVME_SUCCESS;
    }

    if (prinfo & NVME_PRINFO_PRCHK_GUARD) {
        uint64_t crc = crc64_nvme(~0ULL, buf, ns->lbasz);

        if (pil) {
            crc = crc64_nvme(~crc, mbuf, pil);
        }

        trace_pci_nvme_dif_prchk_guard_crc64(be64_to_cpu(dif->g64.guard), crc);

        if (be64_to_cpu(dif->g64.guard) != crc) {
            return NVME_E2E_GUARD_ERROR;
        }
    }

    if (prinfo & NVME_PRINFO_PRCHK_APP) {
        trace_pci_nvme_dif_prchk_apptag(be16_to_cpu(dif->g64.apptag), apptag,
                                        appmask);

        if ((be16_to_cpu(dif->g64.apptag) & appmask) != (apptag & appmask)) {
            return NVME_E2E_APP_ERROR;
        }
    }

    if (prinfo & NVME_PRINFO_PRCHK_REF) {
        trace_pci_nvme_dif_prchk_reftag_crc64(r, reftag);

        if (r != reftag) {
            return NVME_E2E_REF_ERROR;
        }
    }

    return NVME_SUCCESS;
}

static uint16_t nvme_dif_prchk(NvmeNamespace *ns, NvmeDifTuple *dif,
                               uint8_t *buf, uint8_t *mbuf, size_t pil,
                               uint8_t prinfo, uint16_t apptag,
                               uint16_t appmask, uint64_t reftag)
{
    switch (ns->pif) {
    case NVME_PI_GUARD_16:
        return nvme_dif_prchk_crc16(ns, dif, buf, mbuf, pil, prinfo, apptag,
                                    appmask, reftag);
    case NVME_PI_GUARD_64:
        return nvme_dif_prchk_crc64(ns, dif, buf, mbuf, pil, prinfo, apptag,
                                    appmask, reftag);
    }

    abort();
}

uint16_t nvme_dif_check(NvmeNamespace *ns, uint8_t *buf, size_t len,
                        uint8_t *mbuf, size_t mlen, uint8_t prinfo,
                        uint64_t slba, uint16_t apptag,
                        uint16_t appmask, uint64_t *reftag)
{
    uint8_t *bufp, *end = buf + len;
    int16_t pil = 0;
    uint16_t status;

    status = nvme_check_prinfo(ns, prinfo, slba, *reftag);
    if (status) {
        return status;
    }

    if (!(ns->id_ns.dps & NVME_ID_NS_DPS_FIRST_EIGHT)) {
        pil = ns->lbaf.ms - nvme_pi_tuple_size(ns);
    }

    trace_pci_nvme_dif_check(prinfo, ns->lbasz + pil);

    for (bufp = buf; bufp < end; bufp += ns->lbasz, mbuf += ns->lbaf.ms) {
        NvmeDifTuple *dif = (NvmeDifTuple *)(mbuf + pil);
        status = nvme_dif_prchk(ns, dif, bufp, mbuf, pil, prinfo, apptag,
                                appmask, *reftag);
        if (status) {
            /*
             * The first block of a 'raw' image is always allocated, so we
             * cannot reliably know if the block is all zeroes or not. For
             * CRC16 this works fine because the T10 CRC16 is 0x0 for all
             * zeroes, but the Rocksoft CRC64 is not. Thus, if a guard error is
             * detected for the first block, check if it is zeroed and manually
             * set the protection information to all ones to disable protection
             * information checking.
             */
            if (status == NVME_E2E_GUARD_ERROR && slba == 0x0 && bufp == buf) {
                g_autofree uint8_t *zeroes = g_malloc0(ns->lbasz);

                if (memcmp(bufp, zeroes, ns->lbasz) == 0) {
                    memset(mbuf + pil, 0xff, nvme_pi_tuple_size(ns));
                }
            } else {
                return status;
            }
        }

        if (NVME_ID_NS_DPS_TYPE(ns->id_ns.dps) != NVME_ID_NS_DPS_TYPE_3) {
            (*reftag)++;
        }
    }

    return NVME_SUCCESS;
}

uint16_t nvme_dif_mangle_mdata(NvmeNamespace *ns, uint8_t *mbuf, size_t mlen,
                               uint64_t slba)
{
    BlockBackend *blk = ns->blkconf.blk;
    BlockDriverState *bs = blk_bs(blk);

    int64_t moffset = 0, offset = nvme_l2b(ns, slba);
    uint8_t *mbufp, *end;
    bool zeroed;
    int16_t pil = 0;
    int64_t bytes = (mlen / ns->lbaf.ms) << ns->lbaf.ds;
    int64_t pnum = 0;

    Error *err = NULL;


    if (!(ns->id_ns.dps & NVME_ID_NS_DPS_FIRST_EIGHT)) {
        pil = ns->lbaf.ms - nvme_pi_tuple_size(ns);
    }

    do {
        int ret;

        bytes -= pnum;

        ret = bdrv_block_status(bs, offset, bytes, &pnum, NULL, NULL);
        if (ret < 0) {
            error_setg_errno(&err, -ret, "unable to get block status");
            error_report_err(err);

            return NVME_INTERNAL_DEV_ERROR;
        }

        zeroed = !!(ret & BDRV_BLOCK_ZERO);

        trace_pci_nvme_block_status(offset, bytes, pnum, ret, zeroed);

        if (zeroed) {
            mbufp = mbuf + moffset;
            mlen = (pnum >> ns->lbaf.ds) * ns->lbaf.ms;
            end = mbufp + mlen;

            for (; mbufp < end; mbufp += ns->lbaf.ms) {
                memset(mbufp + pil, 0xff, nvme_pi_tuple_size(ns));
            }
        }

        moffset += (pnum >> ns->lbaf.ds) * ns->lbaf.ms;
        offset += pnum;
    } while (pnum != bytes);

    return NVME_SUCCESS;
}

static void nvme_dif_rw_cb(void *opaque, int ret)
{
    NvmeBounceContext *ctx = opaque;
    NvmeRequest *req = ctx->req;
    NvmeNamespace *ns = req->ns;
    BlockBackend *blk = ns->blkconf.blk;

    trace_pci_nvme_dif_rw_cb(nvme_cid(req), blk_name(blk));

    qemu_iovec_destroy(&ctx->data.iov);
    g_free(ctx->data.bounce);

    qemu_iovec_destroy(&ctx->mdata.iov);
    g_free(ctx->mdata.bounce);

    g_free(ctx);

    nvme_rw_complete_cb(req, ret);
}

static void nvme_dif_rw_check_cb(void *opaque, int ret)
{
    NvmeBounceContext *ctx = opaque;
    NvmeRequest *req = ctx->req;
    NvmeNamespace *ns = req->ns;
    NvmeCtrl *n = nvme_ctrl(req);
    NvmeRwCmd *rw = (NvmeRwCmd *)&req->cmd;
    uint64_t slba = le64_to_cpu(rw->slba);
    uint8_t prinfo = NVME_RW_PRINFO(le16_to_cpu(rw->control));
    uint16_t apptag = le16_to_cpu(rw->apptag);
    uint16_t appmask = le16_to_cpu(rw->appmask);
    uint64_t reftag = le32_to_cpu(rw->reftag);
    uint64_t cdw3 = le32_to_cpu(rw->cdw3);
    uint16_t status;

    reftag |= cdw3 << 32;

    trace_pci_nvme_dif_rw_check_cb(nvme_cid(req), prinfo, apptag, appmask,
                                   reftag);

    if (ret) {
        goto out;
    }

    status = nvme_dif_mangle_mdata(ns, ctx->mdata.bounce, ctx->mdata.iov.size,
                                   slba);
    if (status) {
        req->status = status;
        goto out;
    }

    status = nvme_dif_check(ns, ctx->data.bounce, ctx->data.iov.size,
                            ctx->mdata.bounce, ctx->mdata.iov.size, prinfo,
                            slba, apptag, appmask, &reftag);
    if (status) {
        req->status = status;
        goto out;
    }

    status = nvme_bounce_data(n, ctx->data.bounce, ctx->data.iov.size,
                              NVME_TX_DIRECTION_FROM_DEVICE, req);
    if (status) {
        req->status = status;
        goto out;
    }

    if (prinfo & NVME_PRINFO_PRACT && ns->lbaf.ms == nvme_pi_tuple_size(ns)) {
        goto out;
    }

    status = nvme_bounce_mdata(n, ctx->mdata.bounce, ctx->mdata.iov.size,
                               NVME_TX_DIRECTION_FROM_DEVICE, req);
    if (status) {
        req->status = status;
    }

out:
    nvme_dif_rw_cb(ctx, ret);
}

static void nvme_dif_rw_mdata_in_cb(void *opaque, int ret)
{
    NvmeBounceContext *ctx = opaque;
    NvmeRequest *req = ctx->req;
    NvmeNamespace *ns = req->ns;
    NvmeRwCmd *rw = (NvmeRwCmd *)&req->cmd;
    uint64_t slba = le64_to_cpu(rw->slba);
    uint32_t nlb = le16_to_cpu(rw->nlb) + 1;
    size_t mlen = nvme_m2b(ns, nlb);
    uint64_t offset = nvme_moff(ns, slba);
    BlockBackend *blk = ns->blkconf.blk;

    trace_pci_nvme_dif_rw_mdata_in_cb(nvme_cid(req), blk_name(blk));

    if (ret) {
        goto out;
    }

    ctx->mdata.bounce = g_malloc(mlen);

    qemu_iovec_reset(&ctx->mdata.iov);
    qemu_iovec_add(&ctx->mdata.iov, ctx->mdata.bounce, mlen);

    req->aiocb = blk_aio_preadv(blk, offset, &ctx->mdata.iov, 0,
                                nvme_dif_rw_check_cb, ctx);
    return;

out:
    nvme_dif_rw_cb(ctx, ret);
}

static void nvme_dif_rw_mdata_out_cb(void *opaque, int ret)
{
    NvmeBounceContext *ctx = opaque;
    NvmeRequest *req = ctx->req;
    NvmeNamespace *ns = req->ns;
    NvmeRwCmd *rw = (NvmeRwCmd *)&req->cmd;
    uint64_t slba = le64_to_cpu(rw->slba);
    uint64_t offset = nvme_moff(ns, slba);
    BlockBackend *blk = ns->blkconf.blk;

    trace_pci_nvme_dif_rw_mdata_out_cb(nvme_cid(req), blk_name(blk));

    if (ret) {
        goto out;
    }

    req->aiocb = blk_aio_pwritev(blk, offset, &ctx->mdata.iov, 0,
                                 nvme_dif_rw_cb, ctx);
    return;

out:
    nvme_dif_rw_cb(ctx, ret);
}

uint16_t nvme_dif_rw(NvmeCtrl *n, NvmeRequest *req)
{
    NvmeRwCmd *rw = (NvmeRwCmd *)&req->cmd;
    NvmeNamespace *ns = req->ns;
    BlockBackend *blk = ns->blkconf.blk;
    bool wrz = rw->opcode == NVME_CMD_WRITE_ZEROES;
    uint32_t nlb = le16_to_cpu(rw->nlb) + 1;
    uint64_t slba = le64_to_cpu(rw->slba);
    size_t len = nvme_l2b(ns, nlb);
    size_t mlen = nvme_m2b(ns, nlb);
    size_t mapped_len = len;
    int64_t offset = nvme_l2b(ns, slba);
    uint8_t prinfo = NVME_RW_PRINFO(le16_to_cpu(rw->control));
    uint16_t apptag = le16_to_cpu(rw->apptag);
    uint16_t appmask = le16_to_cpu(rw->appmask);
    uint64_t reftag = le32_to_cpu(rw->reftag);
    uint64_t cdw3 = le32_to_cpu(rw->cdw3);
    bool pract = !!(prinfo & NVME_PRINFO_PRACT);
    NvmeBounceContext *ctx;
    uint16_t status;

    reftag |= cdw3 << 32;

    trace_pci_nvme_dif_rw(pract, prinfo);

    ctx = g_new0(NvmeBounceContext, 1);
    ctx->req = req;

    if (wrz) {
        BdrvRequestFlags flags = BDRV_REQ_MAY_UNMAP;

        if (prinfo & NVME_PRINFO_PRCHK_MASK) {
            status = NVME_INVALID_PROT_INFO | NVME_DNR;
            goto err;
        }

        if (pract) {
            uint8_t *mbuf, *end;
            int16_t pil = ns->lbaf.ms - nvme_pi_tuple_size(ns);

            flags = 0;

            ctx->mdata.bounce = g_malloc0(mlen);

            qemu_iovec_init(&ctx->mdata.iov, 1);
            qemu_iovec_add(&ctx->mdata.iov, ctx->mdata.bounce, mlen);

            mbuf = ctx->mdata.bounce;
            end = mbuf + mlen;

            if (ns->id_ns.dps & NVME_ID_NS_DPS_FIRST_EIGHT) {
                pil = 0;
            }

            for (; mbuf < end; mbuf += ns->lbaf.ms) {
                NvmeDifTuple *dif = (NvmeDifTuple *)(mbuf + pil);

                switch (ns->pif) {
                case NVME_PI_GUARD_16:
                    dif->g16.apptag = cpu_to_be16(apptag);
                    dif->g16.reftag = cpu_to_be32(reftag);

                    break;

                case NVME_PI_GUARD_64:
                    dif->g64.guard = cpu_to_be64(0x6482d367eb22b64e);
                    dif->g64.apptag = cpu_to_be16(apptag);

                    dif->g64.sr[0] = reftag >> 40;
                    dif->g64.sr[1] = reftag >> 32;
                    dif->g64.sr[2] = reftag >> 24;
                    dif->g64.sr[3] = reftag >> 16;
                    dif->g64.sr[4] = reftag >> 8;
                    dif->g64.sr[5] = reftag;

                    break;

                default:
                    abort();
                }

                switch (NVME_ID_NS_DPS_TYPE(ns->id_ns.dps)) {
                case NVME_ID_NS_DPS_TYPE_1:
                case NVME_ID_NS_DPS_TYPE_2:
                    reftag++;
                }
            }
        }

        req->aiocb = blk_aio_pwrite_zeroes(blk, offset, len, flags,
                                           nvme_dif_rw_mdata_out_cb, ctx);
        return NVME_NO_COMPLETE;
    }

    if (nvme_ns_ext(ns) && !(pract && ns->lbaf.ms == nvme_pi_tuple_size(ns))) {
        mapped_len += mlen;
    }

    status = nvme_map_dptr(n, &req->sg, mapped_len, &req->cmd);
    if (status) {
        goto err;
    }

    ctx->data.bounce = g_malloc(len);

    qemu_iovec_init(&ctx->data.iov, 1);
    qemu_iovec_add(&ctx->data.iov, ctx->data.bounce, len);

    if (req->cmd.opcode == NVME_CMD_READ) {
        block_acct_start(blk_get_stats(blk), &req->acct, ctx->data.iov.size,
                         BLOCK_ACCT_READ);

        req->aiocb = blk_aio_preadv(ns->blkconf.blk, offset, &ctx->data.iov, 0,
                                    nvme_dif_rw_mdata_in_cb, ctx);
        return NVME_NO_COMPLETE;
    }

    status = nvme_bounce_data(n, ctx->data.bounce, ctx->data.iov.size,
                              NVME_TX_DIRECTION_TO_DEVICE, req);
    if (status) {
        goto err;
    }

    ctx->mdata.bounce = g_malloc(mlen);

    qemu_iovec_init(&ctx->mdata.iov, 1);
    qemu_iovec_add(&ctx->mdata.iov, ctx->mdata.bounce, mlen);

    if (!(pract && ns->lbaf.ms == nvme_pi_tuple_size(ns))) {
        status = nvme_bounce_mdata(n, ctx->mdata.bounce, ctx->mdata.iov.size,
                                   NVME_TX_DIRECTION_TO_DEVICE, req);
        if (status) {
            goto err;
        }
    }

    status = nvme_check_prinfo(ns, prinfo, slba, reftag);
    if (status) {
        goto err;
    }

    if (pract) {
        /* splice generated protection information into the buffer */
        nvme_dif_pract_generate_dif(ns, ctx->data.bounce, ctx->data.iov.size,
                                    ctx->mdata.bounce, ctx->mdata.iov.size,
                                    apptag, &reftag);
    } else {
        status = nvme_dif_check(ns, ctx->data.bounce, ctx->data.iov.size,
                                ctx->mdata.bounce, ctx->mdata.iov.size, prinfo,
                                slba, apptag, appmask, &reftag);
        if (status) {
            goto err;
        }
    }

    block_acct_start(blk_get_stats(blk), &req->acct, ctx->data.iov.size,
                     BLOCK_ACCT_WRITE);

    req->aiocb = blk_aio_pwritev(ns->blkconf.blk, offset, &ctx->data.iov, 0,
                                 nvme_dif_rw_mdata_out_cb, ctx);

    return NVME_NO_COMPLETE;

err:
    qemu_iovec_destroy(&ctx->data.iov);
    g_free(ctx->data.bounce);

    qemu_iovec_destroy(&ctx->mdata.iov);
    g_free(ctx->mdata.bounce);

    g_free(ctx);

    return status;
}
