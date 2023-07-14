/*
 * tpm_ioctl.h
 *
 * (c) Copyright IBM Corporation 2014, 2015.
 *
 * This file is licensed under the terms of the 3-clause BSD license
 */
#ifndef _TPM_IOCTL_H_
#define _TPM_IOCTL_H_

#if defined(__CYGWIN__)
# define __USE_LINUX_IOCTL_DEFS
#endif

#ifndef _WIN32
#include <sys/uio.h>
#include <sys/ioctl.h>
#endif

#ifdef HAVE_SYS_IOCCOM_H
#include <sys/ioccom.h>
#endif

/*
 * Every response from a command involving a TPM command execution must hold
 * the ptm_res as the first element.
 * ptm_res corresponds to the error code of a command executed by the TPM.
 */

typedef uint32_t ptm_res;

/* PTM_GET_TPMESTABLISHED: get the establishment bit */
struct ptm_est {
    union {
        struct {
            ptm_res tpm_result;
            unsigned char bit; /* TPM established bit */
        } resp; /* response */
    } u;
};

/* PTM_RESET_TPMESTABLISHED: reset establishment bit */
struct ptm_reset_est {
    union {
        struct {
            uint8_t loc; /* locality to use */
        } req; /* request */
        struct {
            ptm_res tpm_result;
        } resp; /* response */
    } u;
};

/* PTM_INIT */
struct ptm_init {
    union {
        struct {
            uint32_t init_flags; /* see definitions below */
        } req; /* request */
        struct {
            ptm_res tpm_result;
        } resp; /* response */
    } u;
};

/* above init_flags */
#define PTM_INIT_FLAG_DELETE_VOLATILE (1 << 0)
    /* delete volatile state file after reading it */

/* PTM_SET_LOCALITY */
struct ptm_loc {
    union {
        struct {
            uint8_t loc; /* locality to set */
        } req; /* request */
        struct {
            ptm_res tpm_result;
        } resp; /* response */
    } u;
};

/* PTM_HASH_DATA: hash given data */
struct ptm_hdata {
    union {
        struct {
            uint32_t length;
            uint8_t data[4096];
        } req; /* request */
        struct {
            ptm_res tpm_result;
        } resp; /* response */
    } u;
};

/*
 * size of the TPM state blob to transfer; x86_64 can handle 8k,
 * ppc64le only ~7k; keep the response below a 4k page size
 */
#define PTM_STATE_BLOB_SIZE (3 * 1024)

/*
 * The following is the data structure to get state blobs from the TPM.
 * If the size of the state blob exceeds the PTM_STATE_BLOB_SIZE, multiple reads
 * with this ioctl and with adjusted offset are necessary. All bytes
 * must be transferred and the transfer is done once the last byte has been
 * returned.
 * It is possible to use the read() interface for reading the data; however, the
 * first bytes of the state blob will be part of the response to the ioctl(); a
 * subsequent read() is only necessary if the total length (totlength) exceeds
 * the number of received bytes. seek() is not supported.
 */
struct ptm_getstate {
    union {
        struct {
            uint32_t state_flags; /* may be: PTM_STATE_FLAG_DECRYPTED */
            uint32_t type;        /* which blob to pull */
            uint32_t offset;      /* offset from where to read */
        } req; /* request */
        struct {
            ptm_res tpm_result;
            uint32_t state_flags; /* may be: PTM_STATE_FLAG_ENCRYPTED */
            uint32_t totlength;   /* total length that will be transferred */
            uint32_t length;      /* number of bytes in following buffer */
            uint8_t  data[PTM_STATE_BLOB_SIZE];
        } resp; /* response */
    } u;
};

/* TPM state blob types */
#define PTM_BLOB_TYPE_PERMANENT  1
#define PTM_BLOB_TYPE_VOLATILE   2
#define PTM_BLOB_TYPE_SAVESTATE  3

/* state_flags above : */
#define PTM_STATE_FLAG_DECRYPTED     1 /* on input:  get decrypted state */
#define PTM_STATE_FLAG_ENCRYPTED     2 /* on output: state is encrypted */

/*
 * The following is the data structure to set state blobs in the TPM.
 * If the size of the state blob exceeds the PTM_STATE_BLOB_SIZE, multiple
 * 'writes' using this ioctl are necessary. The last packet is indicated
 * by the length being smaller than the PTM_STATE_BLOB_SIZE.
 * The very first packet may have a length indicator of '0' enabling
 * a write() with all the bytes from a buffer. If the write() interface
 * is used, a final ioctl with a non-full buffer must be made to indicate
 * that all data were transferred (a write with 0 bytes would not work).
 */
struct ptm_setstate {
    union {
        struct {
            uint32_t state_flags; /* may be PTM_STATE_FLAG_ENCRYPTED */
            uint32_t type;        /* which blob to set */
            uint32_t length;      /* length of the data;
                                     use 0 on the first packet to
                                     transfer using write() */
            uint8_t data[PTM_STATE_BLOB_SIZE];
        } req; /* request */
        struct {
            ptm_res tpm_result;
        } resp; /* response */
    } u;
};

/*
 * PTM_GET_CONFIG: Data structure to get runtime configuration information
 * such as which keys are applied.
 */
struct ptm_getconfig {
    union {
        struct {
            ptm_res tpm_result;
            uint32_t flags;
        } resp; /* response */
    } u;
};

#define PTM_CONFIG_FLAG_FILE_KEY        0x1
#define PTM_CONFIG_FLAG_MIGRATION_KEY   0x2

/*
 * PTM_SET_BUFFERSIZE: Set the buffer size to be used by the TPM.
 * A 0 on input queries for the current buffer size. Any other
 * number will try to set the buffer size. The returned number is
 * the buffer size that will be used, which can be larger than the
 * requested one, if it was below the minimum, or smaller than the
 * requested one, if it was above the maximum.
 */
struct ptm_setbuffersize {
    union {
        struct {
            uint32_t buffersize; /* 0 to query for current buffer size */
        } req; /* request */
        struct {
            ptm_res tpm_result;
            uint32_t buffersize; /* buffer size in use */
            uint32_t minsize; /* min. supported buffer size */
            uint32_t maxsize; /* max. supported buffer size */
        } resp; /* response */
    } u;
};

#define PTM_GETINFO_SIZE (3 * 1024)
/*
 * PTM_GET_INFO: Get info about the TPM implementation (from libtpms)
 *
 * This request allows to indirectly call TPMLIB_GetInfo(flags) and
 * retrieve information from libtpms.
 * Only one transaction is currently necessary for returning results
 * to a client. Therefore, totlength and length will be the same if
 * offset is 0.
 */
struct ptm_getinfo {
    union {
        struct {
            uint64_t flags;
            uint32_t offset;      /* offset from where to read */
            uint32_t pad;         /* 32 bit arch */
        } req; /* request */
        struct {
            ptm_res tpm_result;
            uint32_t totlength;
            uint32_t length;
            char buffer[PTM_GETINFO_SIZE];
        } resp; /* response */
    } u;
};

#define SWTPM_INFO_TPMSPECIFICATION ((uint64_t)1 << 0)
#define SWTPM_INFO_TPMATTRIBUTES    ((uint64_t)1 << 1)

/*
 * PTM_LOCK_STORAGE: Lock the storage and retry n times
 */
struct ptm_lockstorage {
    union {
        struct {
            uint32_t retries; /* number of retries */
        } req; /* request */
        struct {
            ptm_res tpm_result;
        } resp; /* response */
    } u;
};

typedef uint64_t ptm_cap;
typedef struct ptm_est ptm_est;
typedef struct ptm_reset_est ptm_reset_est;
typedef struct ptm_loc ptm_loc;
typedef struct ptm_hdata ptm_hdata;
typedef struct ptm_init ptm_init;
typedef struct ptm_getstate ptm_getstate;
typedef struct ptm_setstate ptm_setstate;
typedef struct ptm_getconfig ptm_getconfig;
typedef struct ptm_setbuffersize ptm_setbuffersize;
typedef struct ptm_getinfo ptm_getinfo;
typedef struct ptm_lockstorage ptm_lockstorage;

/* capability flags returned by PTM_GET_CAPABILITY */
#define PTM_CAP_INIT               (1)
#define PTM_CAP_SHUTDOWN           (1 << 1)
#define PTM_CAP_GET_TPMESTABLISHED (1 << 2)
#define PTM_CAP_SET_LOCALITY       (1 << 3)
#define PTM_CAP_HASHING            (1 << 4)
#define PTM_CAP_CANCEL_TPM_CMD     (1 << 5)
#define PTM_CAP_STORE_VOLATILE     (1 << 6)
#define PTM_CAP_RESET_TPMESTABLISHED (1 << 7)
#define PTM_CAP_GET_STATEBLOB      (1 << 8)
#define PTM_CAP_SET_STATEBLOB      (1 << 9)
#define PTM_CAP_STOP               (1 << 10)
#define PTM_CAP_GET_CONFIG         (1 << 11)
#define PTM_CAP_SET_DATAFD         (1 << 12)
#define PTM_CAP_SET_BUFFERSIZE     (1 << 13)
#define PTM_CAP_GET_INFO           (1 << 14)
#define PTM_CAP_SEND_COMMAND_HEADER (1 << 15)
#define PTM_CAP_LOCK_STORAGE       (1 << 16)

#ifndef _WIN32
enum {
    PTM_GET_CAPABILITY     = _IOR('P', 0, ptm_cap),
    PTM_INIT               = _IOWR('P', 1, ptm_init),
    PTM_SHUTDOWN           = _IOR('P', 2, ptm_res),
    PTM_GET_TPMESTABLISHED = _IOR('P', 3, ptm_est),
    PTM_SET_LOCALITY       = _IOWR('P', 4, ptm_loc),
    PTM_HASH_START         = _IOR('P', 5, ptm_res),
    PTM_HASH_DATA          = _IOWR('P', 6, ptm_hdata),
    PTM_HASH_END           = _IOR('P', 7, ptm_res),
    PTM_CANCEL_TPM_CMD     = _IOR('P', 8, ptm_res),
    PTM_STORE_VOLATILE     = _IOR('P', 9, ptm_res),
    PTM_RESET_TPMESTABLISHED = _IOWR('P', 10, ptm_reset_est),
    PTM_GET_STATEBLOB      = _IOWR('P', 11, ptm_getstate),
    PTM_SET_STATEBLOB      = _IOWR('P', 12, ptm_setstate),
    PTM_STOP               = _IOR('P', 13, ptm_res),
    PTM_GET_CONFIG         = _IOR('P', 14, ptm_getconfig),
    PTM_SET_DATAFD         = _IOR('P', 15, ptm_res),
    PTM_SET_BUFFERSIZE     = _IOWR('P', 16, ptm_setbuffersize),
    PTM_GET_INFO           = _IOWR('P', 17, ptm_getinfo),
    PTM_LOCK_STORAGE       = _IOWR('P', 18, ptm_lockstorage),
};
#endif

/*
 * Commands used by the non-CUSE TPMs
 *
 * All messages container big-endian data.
 *
 * The return messages only contain the 'resp' part of the unions
 * in the data structures above. Besides that the limits in the
 * buffers above (ptm_hdata:u.req.data and ptm_get_state:u.resp.data
 * and ptm_set_state:u.req.data) are 0xffffffff.
 */
enum {
    CMD_GET_CAPABILITY = 1,   /* 0x01 */
    CMD_INIT,                 /* 0x02 */
    CMD_SHUTDOWN,             /* 0x03 */
    CMD_GET_TPMESTABLISHED,   /* 0x04 */
    CMD_SET_LOCALITY,         /* 0x05 */
    CMD_HASH_START,           /* 0x06 */
    CMD_HASH_DATA,            /* 0x07 */
    CMD_HASH_END,             /* 0x08 */
    CMD_CANCEL_TPM_CMD,       /* 0x09 */
    CMD_STORE_VOLATILE,       /* 0x0a */
    CMD_RESET_TPMESTABLISHED, /* 0x0b */
    CMD_GET_STATEBLOB,        /* 0x0c */
    CMD_SET_STATEBLOB,        /* 0x0d */
    CMD_STOP,                 /* 0x0e */
    CMD_GET_CONFIG,           /* 0x0f */
    CMD_SET_DATAFD,           /* 0x10 */
    CMD_SET_BUFFERSIZE,       /* 0x11 */
    CMD_GET_INFO,             /* 0x12 */
    CMD_LOCK_STORAGE,         /* 0x13 */
};

#endif /* _TPM_IOCTL_H_ */
