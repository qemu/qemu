#ifndef SCSI_UTILS_H
#define SCSI_UTILS_H

#ifdef CONFIG_LINUX
#include <scsi/sg.h>
#endif

#define SCSI_CMD_BUF_SIZE      16
#define SCSI_SENSE_LEN         18
#define SCSI_SENSE_LEN_SCANNER 32
#define SCSI_INQUIRY_LEN       36

enum SCSIXferMode {
    SCSI_XFER_NONE,      /*  TEST_UNIT_READY, ...            */
    SCSI_XFER_FROM_DEV,  /*  READ, INQUIRY, MODE_SENSE, ...  */
    SCSI_XFER_TO_DEV,    /*  WRITE, MODE_SELECT, ...         */
};

typedef struct SCSICommand {
    uint8_t buf[SCSI_CMD_BUF_SIZE];
    int len;
    size_t xfer;
    uint64_t lba;
    enum SCSIXferMode mode;
} SCSICommand;

typedef struct SCSISense {
    uint8_t key;
    uint8_t asc;
    uint8_t ascq;
} SCSISense;

int scsi_build_sense(uint8_t *buf, SCSISense sense);
SCSISense scsi_parse_sense_buf(const uint8_t *in_buf, int in_len);
int scsi_build_sense_buf(uint8_t *buf, size_t max_size, SCSISense sense,
                         bool fixed_sense);

/*
 * Predefined sense codes
 */

/* No sense data available */
extern const struct SCSISense sense_code_NO_SENSE;
/* LUN not ready, Manual intervention required */
extern const struct SCSISense sense_code_LUN_NOT_READY;
/* LUN not ready, Medium not present */
extern const struct SCSISense sense_code_NO_MEDIUM;
/* LUN not ready, medium removal prevented */
extern const struct SCSISense sense_code_NOT_READY_REMOVAL_PREVENTED;
/* Hardware error, internal target failure */
extern const struct SCSISense sense_code_TARGET_FAILURE;
/* Illegal request, invalid command operation code */
extern const struct SCSISense sense_code_INVALID_OPCODE;
/* Illegal request, LBA out of range */
extern const struct SCSISense sense_code_LBA_OUT_OF_RANGE;
/* Illegal request, Invalid field in CDB */
extern const struct SCSISense sense_code_INVALID_FIELD;
/* Illegal request, Invalid field in parameter list */
extern const struct SCSISense sense_code_INVALID_PARAM;
/* Illegal request, Invalid value in parameter list */
extern const struct SCSISense sense_code_INVALID_PARAM_VALUE;
/* Illegal request, Parameter list length error */
extern const struct SCSISense sense_code_INVALID_PARAM_LEN;
/* Illegal request, LUN not supported */
extern const struct SCSISense sense_code_LUN_NOT_SUPPORTED;
/* Illegal request, Saving parameters not supported */
extern const struct SCSISense sense_code_SAVING_PARAMS_NOT_SUPPORTED;
/* Illegal request, Incompatible format */
extern const struct SCSISense sense_code_INCOMPATIBLE_FORMAT;
/* Illegal request, medium removal prevented */
extern const struct SCSISense sense_code_ILLEGAL_REQ_REMOVAL_PREVENTED;
/* Illegal request, Invalid Transfer Tag */
extern const struct SCSISense sense_code_INVALID_TAG;
/* Command aborted, I/O process terminated */
extern const struct SCSISense sense_code_IO_ERROR;
/* Command aborted, I_T Nexus loss occurred */
extern const struct SCSISense sense_code_I_T_NEXUS_LOSS;
/* Command aborted, Logical Unit failure */
extern const struct SCSISense sense_code_LUN_FAILURE;
/* Command aborted, LUN Communication failure */
extern const struct SCSISense sense_code_LUN_COMM_FAILURE;
/* Command aborted, Overlapped Commands Attempted */
extern const struct SCSISense sense_code_OVERLAPPED_COMMANDS;
/* Medium error, Unrecovered read error */
extern const struct SCSISense sense_code_READ_ERROR;
/* LUN not ready, Cause not reportable */
extern const struct SCSISense sense_code_NOT_READY;
/* Unit attention, Capacity data has changed */
extern const struct SCSISense sense_code_CAPACITY_CHANGED;
/* Unit attention, SCSI bus reset */
extern const struct SCSISense sense_code_SCSI_BUS_RESET;
/* LUN not ready, Medium not present */
extern const struct SCSISense sense_code_UNIT_ATTENTION_NO_MEDIUM;
/* Unit attention, Power on, reset or bus device reset occurred */
extern const struct SCSISense sense_code_RESET;
/* Unit attention, Medium may have changed*/
extern const struct SCSISense sense_code_MEDIUM_CHANGED;
/* Unit attention, Reported LUNs data has changed */
extern const struct SCSISense sense_code_REPORTED_LUNS_CHANGED;
/* Unit attention, Device internal reset */
extern const struct SCSISense sense_code_DEVICE_INTERNAL_RESET;
/* Data Protection, Write Protected */
extern const struct SCSISense sense_code_WRITE_PROTECTED;
/* Data Protection, Space Allocation Failed Write Protect */
extern const struct SCSISense sense_code_SPACE_ALLOC_FAILED;

#define SENSE_CODE(x) sense_code_ ## x

int scsi_sense_to_errno(int key, int asc, int ascq);
int scsi_sense_buf_to_errno(const uint8_t *sense, size_t sense_size);
bool scsi_sense_buf_is_guest_recoverable(const uint8_t *sense, size_t sense_size);

int scsi_convert_sense(uint8_t *in_buf, int in_len,
                       uint8_t *buf, int len, bool fixed);
const char *scsi_command_name(uint8_t cmd);

uint64_t scsi_cmd_lba(SCSICommand *cmd);
uint32_t scsi_data_cdb_xfer(uint8_t *buf);
uint32_t scsi_cdb_xfer(uint8_t *buf);
int scsi_cdb_length(uint8_t *buf);

/* Linux SG_IO interface.  */
#ifdef CONFIG_LINUX
#define SG_ERR_DRIVER_TIMEOUT  0x06
#define SG_ERR_DRIVER_SENSE    0x08

#define SG_ERR_DID_OK          0x00
#define SG_ERR_DID_NO_CONNECT  0x01
#define SG_ERR_DID_BUS_BUSY    0x02
#define SG_ERR_DID_TIME_OUT    0x03

#define SG_ERR_DRIVER_SENSE    0x08

int sg_io_sense_from_errno(int errno_value, struct sg_io_hdr *io_hdr,
                           SCSISense *sense);
#endif

int scsi_sense_from_errno(int errno_value, SCSISense *sense);

#endif
