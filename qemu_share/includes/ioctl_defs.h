#ifndef CXL_SWITCH_IOCTL_DEFS
#define CXL_SWITCH_IOCTL_DEFS

#include <asm-generic/ioctl.h>


// This header file is used by both the kernel module
// and the userspace applications that want to request channels
#ifdef __KERNEL__
    #include <linux/types.h>
    typedef u64 portable_uint64_t;
#else
    #include <stdint.h>
    typedef uint64_t portable_uint64_t;
#endif

// IOCTL Command
typedef struct {
    portable_uint64_t physical_offset;
    portable_uint64_t size;
} cxl_channel_map_info_t;


#define CXL_SWITCH_IOCTL_MAGIC 'c'
#define CXL_SWITCH_IOCTL_SET_EVENTFD_NOTIFY    _IOW(CXL_SWITCH_IOCTL_MAGIC, 1, int)
#define CXL_SWITCH_IOCTL_SET_EVENTFD_CMD_READY _IOW(CXL_SWITCH_IOCTL_MAGIC, 2, int)
#define CXL_SWITCH_IOCTL_MAP_CHANNEL _IOWR(CXL_SWITCH_IOCTL_MAGIC, 3, cxl_channel_map_info_t)

#define REG_COMMAND_DOORBELL 0x00
#define REG_COMMAND_STATUS   0x04
#define REG_NOTIF_STATUS     0x08

#define CMD_STATUS_IDLE                    0x00
#define CMD_STATUS_PROCESSING              0x01
#define CMD_STATUS_RESPONSE_READY          0x02
#define CMD_STATUS_ERROR_IPC               0xE0

#define NOTIF_STATUS_NONE               0x00
#define NOTIF_STATUS_NEW_CLIENT         0x01
#define NOTIF_STATUS_CHANNEL_CLOSED     0x02

#define IRQ_SOURCE_NEW_CLIENT_NOTIFY     (1 << 0)  // 0x01
#define IRQ_SOURCE_CLOSE_CHANNEL_NOTIFY  (1 << 1)  // 0x02  
#define IRQ_SOURCE_CMD_RESPONSE_READY    (1 << 2)  // 0x04
#define ALL_INTERRUPT_SOURCES (IRQ_SOURCE_NEW_CLIENT_NOTIFY | IRQ_SOURCE_CMD_RESPONSE_READY | IRQ_SOURCE_CLOSE_CHANNEL_NOTIFY)

#endif