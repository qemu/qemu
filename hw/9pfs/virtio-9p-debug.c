/*
 * Virtio 9p PDU debug
 *
 * Copyright IBM, Corp. 2010
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "hw/virtio.h"
#include "hw/pc.h"
#include "virtio-9p.h"
#include "virtio-9p-debug.h"

#define BUG_ON(cond) assert(!(cond))

static FILE *llogfile;

static struct iovec *get_sg(V9fsPDU *pdu, int rx)
{
    if (rx) {
        return pdu->elem.in_sg;
    }
    return pdu->elem.out_sg;
}

static int get_sg_count(V9fsPDU *pdu, int rx)
{
    if (rx) {
        return pdu->elem.in_num;
    }
    return pdu->elem.out_num;

}

static void pprint_int8(V9fsPDU *pdu, int rx, size_t *offsetp,
                        const char *name)
{
    size_t copied;
    int count = get_sg_count(pdu, rx);
    size_t offset = *offsetp;
    struct iovec *sg = get_sg(pdu, rx);
    int8_t value;

    copied = do_pdu_unpack(&value, sg, count, offset, sizeof(value));

    BUG_ON(copied != sizeof(value));
    offset += sizeof(value);
    fprintf(llogfile, "%s=0x%x", name, value);
    *offsetp = offset;
}

static void pprint_int16(V9fsPDU *pdu, int rx, size_t *offsetp,
                        const char *name)
{
    size_t copied;
    int count = get_sg_count(pdu, rx);
    struct iovec *sg = get_sg(pdu, rx);
    size_t offset = *offsetp;
    int16_t value;


    copied = do_pdu_unpack(&value, sg, count, offset, sizeof(value));

    BUG_ON(copied != sizeof(value));
    offset += sizeof(value);
    fprintf(llogfile, "%s=0x%x", name, value);
    *offsetp = offset;
}

static void pprint_int32(V9fsPDU *pdu, int rx, size_t *offsetp,
                        const char *name)
{
    size_t copied;
    int count = get_sg_count(pdu, rx);
    struct iovec *sg = get_sg(pdu, rx);
    size_t offset = *offsetp;
    int32_t value;


    copied = do_pdu_unpack(&value, sg, count, offset, sizeof(value));

    BUG_ON(copied != sizeof(value));
    offset += sizeof(value);
    fprintf(llogfile, "%s=0x%x", name, value);
    *offsetp = offset;
}

static void pprint_int64(V9fsPDU *pdu, int rx, size_t *offsetp,
                        const char *name)
{
    size_t copied;
    int count = get_sg_count(pdu, rx);
    struct iovec *sg = get_sg(pdu, rx);
    size_t offset = *offsetp;
    int64_t value;


    copied = do_pdu_unpack(&value, sg, count, offset, sizeof(value));

    BUG_ON(copied != sizeof(value));
    offset += sizeof(value);
    fprintf(llogfile, "%s=0x%" PRIx64, name, value);
    *offsetp = offset;
}

static void pprint_str(V9fsPDU *pdu, int rx, size_t *offsetp, const char *name)
{
    int sg_count = get_sg_count(pdu, rx);
    struct iovec *sg = get_sg(pdu, rx);
    size_t offset = *offsetp;
    uint16_t tmp_size, size;
    size_t result;
    size_t copied = 0;
    int i = 0;

    /* get the size */
    copied = do_pdu_unpack(&tmp_size, sg, sg_count, offset, sizeof(tmp_size));
    BUG_ON(copied != sizeof(tmp_size));
    size = le16_to_cpupu(&tmp_size);
    offset += copied;

    fprintf(llogfile, "%s=", name);
    for (i = 0; size && i < sg_count; i++) {
        size_t len;
        if (offset >= sg[i].iov_len) {
            /* skip this sg */
            offset -= sg[i].iov_len;
            continue;
        } else {
            len = MIN(sg[i].iov_len - offset, size);
            result = fwrite(sg[i].iov_base + offset, 1, len, llogfile);
            BUG_ON(result != len);
            size -= len;
            copied += len;
            if (size) {
                offset = 0;
                continue;
            }
        }
    }
    *offsetp += copied;
}

static void pprint_qid(V9fsPDU *pdu, int rx, size_t *offsetp, const char *name)
{
    fprintf(llogfile, "%s={", name);
    pprint_int8(pdu, rx, offsetp, "type");
    pprint_int32(pdu, rx, offsetp, ", version");
    pprint_int64(pdu, rx, offsetp, ", path");
    fprintf(llogfile, "}");
}

static void pprint_stat(V9fsPDU *pdu, int rx, size_t *offsetp, const char *name)
{
    fprintf(llogfile, "%s={", name);
    pprint_int16(pdu, rx, offsetp, "size");
    pprint_int16(pdu, rx, offsetp, ", type");
    pprint_int32(pdu, rx, offsetp, ", dev");
    pprint_qid(pdu, rx, offsetp, ", qid");
    pprint_int32(pdu, rx, offsetp, ", mode");
    pprint_int32(pdu, rx, offsetp, ", atime");
    pprint_int32(pdu, rx, offsetp, ", mtime");
    pprint_int64(pdu, rx, offsetp, ", length");
    pprint_str(pdu, rx, offsetp, ", name");
    pprint_str(pdu, rx, offsetp, ", uid");
    pprint_str(pdu, rx, offsetp, ", gid");
    pprint_str(pdu, rx, offsetp, ", muid");
    pprint_str(pdu, rx, offsetp, ", extension");
    pprint_int32(pdu, rx, offsetp, ", uid");
    pprint_int32(pdu, rx, offsetp, ", gid");
    pprint_int32(pdu, rx, offsetp, ", muid");
    fprintf(llogfile, "}");
}

static void pprint_stat_dotl(V9fsPDU *pdu, int rx, size_t *offsetp,
                                                  const char *name)
{
    fprintf(llogfile, "%s={", name);
    pprint_qid(pdu, rx, offsetp, "qid");
    pprint_int32(pdu, rx, offsetp, ", st_mode");
    pprint_int64(pdu, rx, offsetp, ", st_nlink");
    pprint_int32(pdu, rx, offsetp, ", st_uid");
    pprint_int32(pdu, rx, offsetp, ", st_gid");
    pprint_int64(pdu, rx, offsetp, ", st_rdev");
    pprint_int64(pdu, rx, offsetp, ", st_size");
    pprint_int64(pdu, rx, offsetp, ", st_blksize");
    pprint_int64(pdu, rx, offsetp, ", st_blocks");
    pprint_int64(pdu, rx, offsetp, ", atime");
    pprint_int64(pdu, rx, offsetp, ", atime_nsec");
    pprint_int64(pdu, rx, offsetp, ", mtime");
    pprint_int64(pdu, rx, offsetp, ", mtime_nsec");
    pprint_int64(pdu, rx, offsetp, ", ctime");
    pprint_int64(pdu, rx, offsetp, ", ctime_nsec");
    fprintf(llogfile, "}");
}



static void pprint_strs(V9fsPDU *pdu, int rx, size_t *offsetp, const char *name)
{
    int sg_count = get_sg_count(pdu, rx);
    struct iovec *sg = get_sg(pdu, rx);
    size_t offset = *offsetp;
    uint16_t tmp_count, count, i;
    size_t copied = 0;

    fprintf(llogfile, "%s={", name);

    /* Get the count */
    copied = do_pdu_unpack(&tmp_count, sg, sg_count, offset, sizeof(tmp_count));
    BUG_ON(copied != sizeof(tmp_count));
    count = le16_to_cpupu(&tmp_count);
    offset += copied;

    for (i = 0; i < count; i++) {
        char str[512];
        if (i) {
            fprintf(llogfile, ", ");
        }
        snprintf(str, sizeof(str), "[%d]", i);
        pprint_str(pdu, rx, &offset, str);
    }

    fprintf(llogfile, "}");

    *offsetp = offset;
}

static void pprint_qids(V9fsPDU *pdu, int rx, size_t *offsetp, const char *name)
{
    int sg_count = get_sg_count(pdu, rx);
    struct iovec *sg = get_sg(pdu, rx);
    size_t offset = *offsetp;
    uint16_t tmp_count, count, i;
    size_t copied = 0;

    fprintf(llogfile, "%s={", name);

    copied = do_pdu_unpack(&tmp_count, sg, sg_count, offset, sizeof(tmp_count));
    BUG_ON(copied != sizeof(tmp_count));
    count = le16_to_cpupu(&tmp_count);
    offset += copied;

    for (i = 0; i < count; i++) {
        char str[512];
        if (i) {
            fprintf(llogfile, ", ");
        }
        snprintf(str, sizeof(str), "[%d]", i);
        pprint_qid(pdu, rx, &offset, str);
    }

    fprintf(llogfile, "}");

    *offsetp = offset;
}

static void pprint_sg(V9fsPDU *pdu, int rx, size_t *offsetp, const char *name)
{
    struct iovec *sg = get_sg(pdu, rx);
    unsigned int count;
    int i;

    if (rx) {
        count = pdu->elem.in_num;
    } else {
        count = pdu->elem.out_num;
    }

    fprintf(llogfile, "%s={", name);
    for (i = 0; i < count; i++) {
        if (i) {
            fprintf(llogfile, ", ");
        }
        fprintf(llogfile, "(%p, 0x%zx)", sg[i].iov_base, sg[i].iov_len);
    }
    fprintf(llogfile, "}");
}

/* FIXME: read from a directory fid returns serialized stat_t's */
#ifdef DEBUG_DATA
static void pprint_data(V9fsPDU *pdu, int rx, size_t *offsetp, const char *name)
{
    struct iovec *sg = get_sg(pdu, rx);
    size_t offset = *offsetp;
    unsigned int count;
    int32_t size;
    int total, i, j;
    ssize_t len;

    if (rx) {
        count = pdu->elem.in_num;
    } else {
        count = pdu->elem.out_num;
    }

    BUG_ON((offset + sizeof(size)) > sg[0].iov_len);

    memcpy(&size, sg[0].iov_base + offset, sizeof(size));
    offset += sizeof(size);

    fprintf(llogfile, "size: %x\n", size);

    sg[0].iov_base += 11; /* skip header */
    sg[0].iov_len -= 11;

    total = 0;
    for (i = 0; i < count; i++) {
        total += sg[i].iov_len;
        if (total >= size) {
            /* trim sg list so writev does the right thing */
            sg[i].iov_len -= (total - size);
            i++;
            break;
        }
    }

    fprintf(llogfile, "%s={\"", name);
    fflush(llogfile);
    for (j = 0; j < i; j++) {
        if (j) {
            fprintf(llogfile, "\", \"");
            fflush(llogfile);
        }

        do {
            len = writev(fileno(llogfile), &sg[j], 1);
        } while (len == -1 && errno == EINTR);
        fprintf(llogfile, "len == %ld: %m\n", len);
        BUG_ON(len != sg[j].iov_len);
    }
    fprintf(llogfile, "\"}");

    sg[0].iov_base -= 11;
    sg[0].iov_len += 11;

}
#endif

void pprint_pdu(V9fsPDU *pdu)
{
    size_t offset = 7;

    if (llogfile == NULL) {
        llogfile = fopen("/tmp/pdu.log", "w");
    }

    BUG_ON(!llogfile);

    switch (pdu->id) {
    case P9_TREADDIR:
        fprintf(llogfile, "TREADDIR: (");
        pprint_int32(pdu, 0, &offset, "fid");
        pprint_int64(pdu, 0, &offset, ", initial offset");
        pprint_int32(pdu, 0, &offset, ", max count");
        break;
    case P9_RREADDIR:
        fprintf(llogfile, "RREADDIR: (");
        pprint_int32(pdu, 1, &offset, "count");
#ifdef DEBUG_DATA
        pprint_data(pdu, 1, &offset, ", data");
#endif
        break;
    case P9_TMKDIR:
        fprintf(llogfile, "TMKDIR: (");
        pprint_int32(pdu, 0, &offset, "fid");
        pprint_str(pdu, 0, &offset, "name");
        pprint_int32(pdu, 0, &offset, "mode");
        pprint_int32(pdu, 0, &offset, "gid");
        break;
    case P9_RMKDIR:
        fprintf(llogfile, "RMKDIR: (");
        pprint_qid(pdu, 0, &offset, "qid");
        break;
    case P9_TVERSION:
        fprintf(llogfile, "TVERSION: (");
        pprint_int32(pdu, 0, &offset, "msize");
        pprint_str(pdu, 0, &offset, ", version");
        break;
    case P9_RVERSION:
        fprintf(llogfile, "RVERSION: (");
        pprint_int32(pdu, 1, &offset, "msize");
        pprint_str(pdu, 1, &offset, ", version");
        break;
    case P9_TGETATTR:
        fprintf(llogfile, "TGETATTR: (");
        pprint_int32(pdu, 0, &offset, "fid");
        break;
    case P9_RGETATTR:
        fprintf(llogfile, "RGETATTR: (");
        pprint_stat_dotl(pdu, 1, &offset, "getattr");
        break;
    case P9_TAUTH:
        fprintf(llogfile, "TAUTH: (");
        pprint_int32(pdu, 0, &offset, "afid");
        pprint_str(pdu, 0, &offset, ", uname");
        pprint_str(pdu, 0, &offset, ", aname");
        pprint_int32(pdu, 0, &offset, ", n_uname");
        break;
    case P9_RAUTH:
        fprintf(llogfile, "RAUTH: (");
        pprint_qid(pdu, 1, &offset, "qid");
        break;
    case P9_TATTACH:
        fprintf(llogfile, "TATTACH: (");
        pprint_int32(pdu, 0, &offset, "fid");
        pprint_int32(pdu, 0, &offset, ", afid");
        pprint_str(pdu, 0, &offset, ", uname");
        pprint_str(pdu, 0, &offset, ", aname");
        pprint_int32(pdu, 0, &offset, ", n_uname");
        break;
    case P9_RATTACH:
        fprintf(llogfile, "RATTACH: (");
        pprint_qid(pdu, 1, &offset, "qid");
        break;
    case P9_TERROR:
        fprintf(llogfile, "TERROR: (");
        break;
    case P9_RERROR:
        fprintf(llogfile, "RERROR: (");
        pprint_str(pdu, 1, &offset, "ename");
        pprint_int32(pdu, 1, &offset, ", ecode");
        break;
    case P9_TFLUSH:
        fprintf(llogfile, "TFLUSH: (");
        pprint_int16(pdu, 0, &offset, "oldtag");
        break;
    case P9_RFLUSH:
        fprintf(llogfile, "RFLUSH: (");
        break;
    case P9_TWALK:
        fprintf(llogfile, "TWALK: (");
        pprint_int32(pdu, 0, &offset, "fid");
        pprint_int32(pdu, 0, &offset, ", newfid");
        pprint_strs(pdu, 0, &offset, ", wnames");
        break;
    case P9_RWALK:
        fprintf(llogfile, "RWALK: (");
        pprint_qids(pdu, 1, &offset, "wqids");
        break;
    case P9_TOPEN:
        fprintf(llogfile, "TOPEN: (");
        pprint_int32(pdu, 0, &offset, "fid");
        pprint_int8(pdu, 0, &offset, ", mode");
        break;
    case P9_ROPEN:
        fprintf(llogfile, "ROPEN: (");
        pprint_qid(pdu, 1, &offset, "qid");
        pprint_int32(pdu, 1, &offset, ", iounit");
        break;
    case P9_TCREATE:
        fprintf(llogfile, "TCREATE: (");
        pprint_int32(pdu, 0, &offset, "fid");
        pprint_str(pdu, 0, &offset, ", name");
        pprint_int32(pdu, 0, &offset, ", perm");
        pprint_int8(pdu, 0, &offset, ", mode");
        pprint_str(pdu, 0, &offset, ", extension");
        break;
    case P9_RCREATE:
        fprintf(llogfile, "RCREATE: (");
        pprint_qid(pdu, 1, &offset, "qid");
        pprint_int32(pdu, 1, &offset, ", iounit");
        break;
    case P9_TSYMLINK:
        fprintf(llogfile, "TSYMLINK: (");
        pprint_int32(pdu, 0, &offset, "fid");
        pprint_str(pdu, 0, &offset, ", name");
        pprint_str(pdu, 0, &offset, ", symname");
        pprint_int32(pdu, 0, &offset, ", gid");
        break;
    case P9_RSYMLINK:
        fprintf(llogfile, "RSYMLINK: (");
        pprint_qid(pdu, 1, &offset, "qid");
        break;
    case P9_TLCREATE:
        fprintf(llogfile, "TLCREATE: (");
        pprint_int32(pdu, 0, &offset, "dfid");
        pprint_str(pdu, 0, &offset, ", name");
        pprint_int32(pdu, 0, &offset, ", flags");
        pprint_int32(pdu, 0, &offset, ", mode");
        pprint_int32(pdu, 0, &offset, ", gid");
        break;
    case P9_RLCREATE:
        fprintf(llogfile, "RLCREATE: (");
        pprint_qid(pdu, 1, &offset, "qid");
        pprint_int32(pdu, 1, &offset, ", iounit");
        break;
    case P9_TMKNOD:
	fprintf(llogfile, "TMKNOD: (");
        pprint_int32(pdu, 0, &offset, "fid");
        pprint_str(pdu, 0, &offset, "name");
        pprint_int32(pdu, 0, &offset, "mode");
        pprint_int32(pdu, 0, &offset, "major");
        pprint_int32(pdu, 0, &offset, "minor");
        pprint_int32(pdu, 0, &offset, "gid");
        break;
    case P9_RMKNOD:
        fprintf(llogfile, "RMKNOD: )");
        pprint_qid(pdu, 0, &offset, "qid");
        break;
    case P9_TREADLINK:
	fprintf(llogfile, "TREADLINK: (");
        pprint_int32(pdu, 0, &offset, "fid");
        break;
    case P9_RREADLINK:
	fprintf(llogfile, "RREADLINK: (");
        pprint_str(pdu, 0, &offset, "target");
        break;
    case P9_TREAD:
        fprintf(llogfile, "TREAD: (");
        pprint_int32(pdu, 0, &offset, "fid");
        pprint_int64(pdu, 0, &offset, ", offset");
        pprint_int32(pdu, 0, &offset, ", count");
        pprint_sg(pdu, 0, &offset, ", sg");
        break;
    case P9_RREAD:
        fprintf(llogfile, "RREAD: (");
        pprint_int32(pdu, 1, &offset, "count");
        pprint_sg(pdu, 1, &offset, ", sg");
        offset = 7;
#ifdef DEBUG_DATA
        pprint_data(pdu, 1, &offset, ", data");
#endif
        break;
    case P9_TWRITE:
        fprintf(llogfile, "TWRITE: (");
        pprint_int32(pdu, 0, &offset, "fid");
        pprint_int64(pdu, 0, &offset, ", offset");
        pprint_int32(pdu, 0, &offset, ", count");
        break;
    case P9_RWRITE:
        fprintf(llogfile, "RWRITE: (");
        pprint_int32(pdu, 1, &offset, "count");
        break;
    case P9_TCLUNK:
        fprintf(llogfile, "TCLUNK: (");
        pprint_int32(pdu, 0, &offset, "fid");
        break;
    case P9_RCLUNK:
        fprintf(llogfile, "RCLUNK: (");
        break;
    case P9_TFSYNC:
        fprintf(llogfile, "TFSYNC: (");
        pprint_int32(pdu, 0, &offset, "fid");
        break;
    case P9_RFSYNC:
        fprintf(llogfile, "RFSYNC: (");
        break;
    case P9_TLINK:
        fprintf(llogfile, "TLINK: (");
        pprint_int32(pdu, 0, &offset, "dfid");
        pprint_int32(pdu, 0, &offset, ", fid");
        pprint_str(pdu, 0, &offset, ", newpath");
        break;
    case P9_RLINK:
        fprintf(llogfile, "RLINK: (");
        break;
    case P9_TREMOVE:
        fprintf(llogfile, "TREMOVE: (");
        pprint_int32(pdu, 0, &offset, "fid");
        break;
    case P9_RREMOVE:
        fprintf(llogfile, "RREMOVE: (");
        break;
    case P9_TSTAT:
        fprintf(llogfile, "TSTAT: (");
        pprint_int32(pdu, 0, &offset, "fid");
        break;
    case P9_RSTAT:
        fprintf(llogfile, "RSTAT: (");
        offset += 2; /* ignored */
        pprint_stat(pdu, 1, &offset, "stat");
        break;
    case P9_TWSTAT:
        fprintf(llogfile, "TWSTAT: (");
        pprint_int32(pdu, 0, &offset, "fid");
        offset += 2; /* ignored */
        pprint_stat(pdu, 0, &offset, ", stat");
        break;
    case P9_RWSTAT:
        fprintf(llogfile, "RWSTAT: (");
        break;
    case P9_TXATTRWALK:
        fprintf(llogfile, "TXATTRWALK: (");
        pprint_int32(pdu, 0, &offset, "fid");
        pprint_int32(pdu, 0, &offset, ", newfid");
        pprint_str(pdu, 0, &offset, ", xattr name");
        break;
    case P9_RXATTRWALK:
        fprintf(llogfile, "RXATTRWALK: (");
        pprint_int64(pdu, 1, &offset, "xattrsize");
    case P9_TXATTRCREATE:
        fprintf(llogfile, "TXATTRCREATE: (");
        pprint_int32(pdu, 0, &offset, "fid");
        pprint_str(pdu, 0, &offset, ", name");
        pprint_int64(pdu, 0, &offset, ", xattrsize");
        pprint_int32(pdu, 0, &offset, ", flags");
        break;
    case P9_RXATTRCREATE:
        fprintf(llogfile, "RXATTRCREATE: (");
        break;
    case P9_TLOCK:
        fprintf(llogfile, "TLOCK: (");
        pprint_int32(pdu, 0, &offset, "fid");
        pprint_int8(pdu, 0, &offset, ", type");
        pprint_int32(pdu, 0, &offset, ", flags");
        pprint_int64(pdu, 0, &offset, ", start");
        pprint_int64(pdu, 0, &offset, ", length");
        pprint_int32(pdu, 0, &offset, ", proc_id");
        pprint_str(pdu, 0, &offset, ", client_id");
        break;
    case P9_RLOCK:
        fprintf(llogfile, "RLOCK: (");
        pprint_int8(pdu, 0, &offset, "status");
        break;
    case P9_TGETLOCK:
        fprintf(llogfile, "TGETLOCK: (");
        pprint_int32(pdu, 0, &offset, "fid");
        pprint_int8(pdu, 0, &offset, ", type");
        pprint_int64(pdu, 0, &offset, ", start");
        pprint_int64(pdu, 0, &offset, ", length");
        pprint_int32(pdu, 0, &offset, ", proc_id");
        pprint_str(pdu, 0, &offset, ", client_id");
        break;
    case P9_RGETLOCK:
        fprintf(llogfile, "RGETLOCK: (");
        pprint_int8(pdu, 0, &offset, "type");
        pprint_int64(pdu, 0, &offset, ", start");
        pprint_int64(pdu, 0, &offset, ", length");
        pprint_int32(pdu, 0, &offset, ", proc_id");
        pprint_str(pdu, 0, &offset, ", client_id");
        break;
    default:
        fprintf(llogfile, "unknown(%d): (", pdu->id);
        break;
    }

    fprintf(llogfile, ")\n");
    /* Flush the log message out */
    fflush(llogfile);
}
