#ifndef CXL_SWITCH_IOCTL_DEFS
#define CXL_SWITCH_IOCTL_DEFS

#include <asm-generic/ioctl.h>

#define CXL_SWITCH_IOCTL_MAGIC 'c'
#define CXL_SWITCH_IOCTL_SET_EVENTFD_NOTIFY    _IOW(CXL_SWITCH_IOCTL_MAGIC, 1, int)
#define CXL_SWITCH_IOCTL_SET_EVENTFD_CMD_READY _IOW(CXL_SWITCH_IOCTL_MAGIC, 2, int)

#define REG_COMMAND_DOORBELL 0x00
#define REG_COMMAND_STATUS   0x04
#define REG_NOTIF_STATUS     0x08

#define CMD_STATUS_IDLE                    0x00
#define CMD_STATUS_PROCESSING              0x01
#define CMD_STATUS_RESPONSE_READY          0x02
#define CMD_STATUS_ERROR_IPC               0xE0

#define NOTIF_STATUS_NONE               0x00
#define NOTIF_STATUS_NEW_CLIENT         0x01

#endif