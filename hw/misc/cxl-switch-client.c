#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/thread.h"
#include "qemu/typedefs.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "hw/qdev-properties.h"
#include "qemu/timer.h"
#include "qom/object.h"
#include "qemu/main-loop.h" /* iothread mutex */
#include "qemu/module.h"
#include "qapi/visitor.h"
#include "standard-headers/linux/pci_regs.h"
#include "system/memory.h"
#include "system/hostmem.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "hw/misc/cxl_switch_ipc.h"

#define CXL_SWITCH_DEBUG 1
#define CXL_SWITCH_DPRINTF(fmt, ...)                       \
    do {                                                   \
        if (CXL_SWITCH_DEBUG) {                            \
            printf("CXL Switch Client: " fmt, ## __VA_ARGS__);    \
        }                                                  \
    } while (0)
#define TYPE_PCI_CXL_SWITCH_CLIENT "cxl-switch-client"

// --- BAR Sizes and layout ---
#define BAR0_MAILBOX_SIZE  0x1000    // 4KB for management mailbox
#define BAR1_CONTROL_SIZE  0x1000    // 4KB for control registers
#define BAR2_DATA_SIZE     (256 * MiB) // Default size

// BAR1 Control Registers
#define REG_COMMAND_DOORBELL 0x00
#define REG_COMMAND_STATUS   0x04
#define REG_NOTIF_STATUS     0x08
#define REG_INTERRUPT_MASK   0x0C
#define REG_INTERRUPT_STATUS 0x10

// Status values for REG_COMMAND_STATUS
#define CMD_STATUS_IDLE                    0x00
#define CMD_STATUS_PROCESSING              0x01
#define CMD_STATUS_RESPONSE_READY          0x02
#define CMD_STATUS_ERROR_IPC               0xE0
#define CMD_STATUS_ERROR_SERVER            0xE1
#define CMD_STATUS_ERROR_INTERNAL          0xE2
#define CMD_STATUS_ERROR_BUSY              0xE3
#define CMD_STATUS_ERROR_BAD_WINDOW_CONFIG 0xE4

// Status values for REG_NOTIF_STATUS
#define NOTIF_STATUS_NONE               0x00
#define NOTIF_STATUS_NEW_CLIENT         0x01

// Bits for REG_INTERRUPT_MASK and REG_INTERRUPT_STATUS
#define IRQ_SOURCE_NEW_CLIENT_NOTIFY  (1 << 0)
#define IRQ_SOURCE_CMD_RESPONSE_READY (1 << 1)

typedef struct CXLSwitchClientState CXLSwitchClientState;
DECLARE_INSTANCE_CHECKER(CXLSwitchClientState, CXL_SWITCH_CLIENT, TYPE_PCI_CXL_SWITCH_CLIENT)

#define PCI_VENDOR_ID_QEMU_CXL_SWITCH 0x1AF4
#define PCI_CXL_DEVICE_ID 0x1337

struct CXLSwitchClientState {
    PCIDevice pdev;

    /**
        Mailbox region used by the user-space RPC library
        to place command structures inside, such as
        register service X or request connection.

        Separates data payload from signal/status
    */
    MemoryRegion bar0_mailbox_region;
    uint8_t bar0_mailbox[BAR0_MAILBOX_SIZE];

    /**
        Small MMIO region for explicit control signal
    */
    MemoryRegion bar1_control_region;
    uint32_t command_status_reg;
    uint32_t notif_status_reg;
    uint32_t interrupt_mask_reg;
    uint32_t interrupt_status_reg;

    /**
        This is a hacky attempt at a Dynamic Capacity Device.
        When a PCIe device is configured, the enumeration process requires
        the PCIe device to report the characteristics of its BARs to the host
        system. This includes the type of resource (memory or I/O), whether
        it's 64 bit prefetchable, and importantly, the size of the memory region
        it requires. The host then allocates a physical address range in the
        system's memory map for this BAR and programs the BAR register on the
        PCI device with the allocated base address.
        Since QEMU adheres to this model, we have no choice but to present this
        static size.
        In our case, our CXL Switch Client wants to request for a shared region
        of the total CXL memory pooling (which is for the RPC connection). This
        is handled via the RPC_SET_BAR2_WINDOW_REQ command. This returns the
        Bar2 window offset and size, which is later referenced by the 
        memory operations on the bar2.
        TODO: Align this with an actual DCD and how CXL presents itself. 
        I am unsure how much engineering effort this will require, hence 
        sticking to the hack for now, but the effort does seem to be a lot,
        given that it took moking sometime to get right.
        Re: https://github.com/moking/qemu/tree/dcd-v6
    */
    MemoryRegion bar2_data_region;
    uint64_t bar2_data_size;           // Actual size of BAR2
    uint64_t bar2_data_window_offset;  // Offset in the global pool
    uint64_t bar2_data_window_size;    // Size that the window is configured

    uint64_t total_pool_size;          // Total size of mem pool

    char *server_socket_path;  // QOM property
    int server_fd;
    QemuMutex lock; // Serialize access to server_fd from MMIO callbacks
};

// --- Forward declarations for MSI ---
static void cxl_server_fd_read_handler(void *opaque);
static void trigger_msi(CXLSwitchClientState *s);

// --- Forward declarations for MemoryRegionOps ---
static uint64_t bar0_mailbox_read(void *opaque, hwaddr addr, unsigned size);
static void bar0_mailbox_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t bar1_control_read(void *opaque, hwaddr addr, unsigned size);
static void bar1_control_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t bar2_data_window_read(void *opaque, hwaddr addr, unsigned size);
static void bar2_data_window_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);

// --- MemoryRegionOps definitions ---
static const MemoryRegionOps bar0_mailbox_ops = {
    .read = bar0_mailbox_read,
    .write = bar0_mailbox_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
};

static const MemoryRegionOps bar1_control_ops = {
    .read = bar1_control_read,
    .write = bar1_control_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static const MemoryRegionOps bar2_data_ops = {
    .read = bar2_data_window_read,
    .write = bar2_data_window_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
};

static void pci_cxl_switch_client_register_types(void);
static void cxl_switch_client_instance_init(Object *obj);
static void cxl_switch_client_class_init(ObjectClass *class, const void *data);
static void pci_cxl_switch_client_realize(PCIDevice *pdev, Error **errp);
static void pci_cxl_switch_client_uninit(PCIDevice *pdev);

// Helper function to send a request and receive a response from the server
// A simple blocking implementation
static int cxl_switch_client_ipc_request_response(CXLSwitchClientState *s,
                                                  const void *req_buf, size_t req_size,
                                                  void *resp_buf, size_t resp_size)
{
    qemu_mutex_lock(&s->lock);

    if (s->server_fd < 0) {
        CXL_SWITCH_DPRINTF("Error: Server socket not initialized.\n");
        qemu_mutex_unlock(&s->lock);
        return -1;
    }

    ssize_t bytes_sent = send(s->server_fd, req_buf, req_size, 0);
    if ((size_t) bytes_sent != req_size) {
        CXL_SWITCH_DPRINTF("Error: Failed to send request to server. Sent %zd bytes, expected %zu.\n", bytes_sent, req_size);
        qemu_mutex_unlock(&s->lock);
        return -1;
    }

    ssize_t bytes_received = recv(s->server_fd, resp_buf, resp_size, 0);
    if ((size_t) bytes_received != resp_size) {
        CXL_SWITCH_DPRINTF("Error: Failed to receive response from server. Received %zd bytes, expected %zu.\n", bytes_received, resp_size);
        qemu_mutex_unlock(&s->lock);
        return -1;
    }

    qemu_mutex_unlock(&s->lock);
    return 0;
}

// --- Bar0 Mailbox Operations ---
static uint64_t bar0_mailbox_read(void *opaque, hwaddr addr, unsigned size)
{
    CXLSwitchClientState *s = opaque;
    uint64_t data = ~0ULL;
    qemu_mutex_lock(&s->lock);

    if ((addr + size) > BAR0_MAILBOX_SIZE) {
        CXL_SWITCH_DPRINTF("GuestError: Read out of bounds (offset=0x%"PRIx64", size=%u, limit=%u)\n",
                      addr, size, BAR0_MAILBOX_SIZE);
        goto out;
    }

    CXL_SWITCH_DPRINTF("Info: Reading from BAR0 mailbox at offset 0x%"PRIx64", size=%u\n",
                  addr, size);
    switch (size) {
        case 1: data = s->bar0_mailbox[addr]; break;
        case 2: data = le16_to_cpu(*(uint16_t *)&s->bar0_mailbox[addr]); break;
        case 4: data = le32_to_cpu(*(uint32_t *)&s->bar0_mailbox[addr]); break;
        case 8: data = le64_to_cpu(*(uint64_t *)&s->bar0_mailbox[addr]); break;
        default: CXL_SWITCH_DPRINTF("Error: Invalid size %u for BAR0 mailbox read.\n", size); break;
    }
out:
    qemu_mutex_unlock(&s->lock);
    return data;
}

static void bar0_mailbox_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    CXLSwitchClientState *s = opaque;
    qemu_mutex_lock(&s->lock);

    if ((addr + size) > BAR0_MAILBOX_SIZE) {
        CXL_SWITCH_DPRINTF("GuestError: Write out of bounds (offset=0x%"PRIx64", size=%u, limit=%u)\n",
                      addr, size, BAR0_MAILBOX_SIZE);
        goto out;
    }

    CXL_SWITCH_DPRINTF("Info: Writing to BAR0 mailbox at offset 0x%"PRIx64", size=%u, value=0x%"PRIx64"\n",
                  addr, size, val);
    switch (size) {
        case 1: s->bar0_mailbox[addr] = (uint8_t)val; break;
        case 2: *(uint16_t *)&s->bar0_mailbox[addr] = cpu_to_le16((uint16_t)val); break;
        case 4: *(uint32_t *)&s->bar0_mailbox[addr] = cpu_to_le32((uint32_t)val); break;
        case 8: *(uint64_t *)&s->bar0_mailbox[addr] = cpu_to_le64(val); break;
        default: CXL_SWITCH_DPRINTF("Error: Invalid size %u for BAR0 mailbox write.\n", size); break;
    }
out:
    qemu_mutex_unlock(&s->lock);
}

// --- Bar1 Control Operations ---
static uint64_t bar1_control_read(void *opaque, hwaddr addr, unsigned size)
{
    CXLSwitchClientState *s = opaque;
    uint32_t reg_val = 0xFFFFFFFF;
    if (size != 4) return reg_val;

    qemu_mutex_lock(&s->lock);
    switch (addr) {
        case REG_COMMAND_STATUS: reg_val = s->command_status_reg; break;
        case REG_NOTIF_STATUS: reg_val = s->notif_status_reg; break;
        case REG_INTERRUPT_MASK: reg_val = s->interrupt_mask_reg; break;
        case REG_INTERRUPT_STATUS: reg_val = s->interrupt_status_reg; break;
        default: 
            CXL_SWITCH_DPRINTF("Error: Invalid address 0x%"PRIx64" for BAR1 control read.\n", addr);
            break;
    }
    qemu_mutex_unlock(&s->lock);
    CXL_SWITCH_DPRINTF("Info: Reading from BAR1 control at offset 0x%"PRIx64", size=%u, value=0x%08x\n",
                  addr, size, reg_val);
    return reg_val;
}

static void bar1_control_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    CXLSwitchClientState *s = opaque;
    uint8_t cmd_type;
    uint8_t tmp_status;

    if (size != 4) return;
    bool triggers_msi = false;
    qemu_mutex_lock(&s->lock);
    switch (addr) {
        case REG_COMMAND_DOORBELL:
            CXL_SWITCH_DPRINTF("Info: Writing to command doorbell with value=0x%08"PRIx64". Current cmd_status=0x%x\n", val, s->command_status_reg);
            if (s->command_status_reg == CMD_STATUS_PROCESSING) {
                CXL_SWITCH_DPRINTF("Error: Command already processing, cannot write new command.\n");
                goto cmd_done_unlock_no_msi;
            }

            s->command_status_reg = CMD_STATUS_PROCESSING;
            // Clear response ready status
            s->interrupt_status_reg &= ~IRQ_SOURCE_CMD_RESPONSE_READY;
            // Release lock before the blocking IPC
            qemu_mutex_unlock(&s->lock);

            cmd_type = s->bar0_mailbox[0];
            CXL_SWITCH_DPRINTF("Info: Command type from mailbox: 0x%02x\n", cmd_type);

            int ipc_ret = -1;
            uint8_t ipc_server_resp_payload[BAR0_MAILBOX_SIZE];
            size_t expected_server_resp_len = 0;

            switch (cmd_type) {
                case CXL_MSG_TYPE_RPC_REGISTER_SERVICE_REQ:
                    expected_server_resp_len = sizeof(cxl_ipc_rpc_register_service_resp_t);
                    CXL_SWITCH_DPRINTF("Info: Handling RPC_REGISTER_SERVICE request.\n");
                    ipc_ret = cxl_switch_client_ipc_request_response(s, s->bar0_mailbox, sizeof(cxl_ipc_rpc_register_service_req_t), ipc_server_resp_payload, expected_server_resp_len);
                    break;
                case CXL_MSG_TYPE_RPC_DEREGISTER_SERVICE_REQ:
                    expected_server_resp_len = sizeof(cxl_ipc_rpc_deregister_service_resp_t);
                    CXL_SWITCH_DPRINTF("Info: Handling RPC_DEREGISTER_SERVICE request.\n");
                    ipc_ret = cxl_switch_client_ipc_request_response(s, s->bar0_mailbox, sizeof(cxl_ipc_rpc_deregister_service_req_t), ipc_server_resp_payload, expected_server_resp_len);
                    break;
                case CXL_MSG_TYPE_RPC_REQUEST_CHANNEL_REQ:
                    expected_server_resp_len = sizeof(cxl_ipc_rpc_request_channel_resp_t);
                    CXL_SWITCH_DPRINTF("Info: Handling RPC_REQUEST_CHANNEL request.\n");
                    ipc_ret = cxl_switch_client_ipc_request_response(s, s->bar0_mailbox, sizeof(cxl_ipc_rpc_request_channel_req_t), ipc_server_resp_payload, expected_server_resp_len);
                    break;
                case CXL_MSG_TYPE_RPC_RELEASE_CHANNEL_REQ:
                    expected_server_resp_len = sizeof(cxl_ipc_rpc_release_channel_resp_t);
                    CXL_SWITCH_DPRINTF("Info: Handling RPC_RELEASE_CHANNEL request.\n");
                    ipc_ret = cxl_switch_client_ipc_request_response(s, s->bar0_mailbox, sizeof(cxl_ipc_rpc_release_channel_req_t), ipc_server_resp_payload, expected_server_resp_len);
                    break;
                
                case CXL_MSG_TYPE_RPC_SET_BAR2_WINDOW_REQ: {
                    // Local QEMU Device command
                    cxl_ipc_rpc_set_bar2_window_req_t *set_window_req = (cxl_ipc_rpc_set_bar2_window_req_t *)s->bar0_mailbox;
                    cxl_ipc_rpc_set_bar2_window_resp_t set_window_resp;
                    set_window_resp.type = CXL_MSG_TYPE_RPC_SET_BAR2_WINDOW_RESP;
            
                    CXL_SWITCH_DPRINTF("Info: Handling RPC_SET_BAR2_WINDOW request. Offset=0x%"PRIx64", Size=0x%"PRIx64"\n",
                                    set_window_req->offset, set_window_req->size);
                    
                    if (set_window_req->size <= s->bar2_data_size && (set_window_req->offset + set_window_req->size) <= s->total_pool_size) {
                        s->bar2_data_window_offset = set_window_req->offset;
                        s->bar2_data_window_size = set_window_req->size;
                        set_window_resp.status = CXL_IPC_STATUS_OK;
                        CXL_SWITCH_DPRINTF("Info: BAR2 window set successfully. Offset=0x%"PRIx64", Size=0x%"PRIx64"\n",
                                        s->bar2_data_window_offset, s->bar2_data_window_size);
                    } else {
                        set_window_resp.status = CXL_IPC_STATUS_BAR2_FAILED;
                        CXL_SWITCH_DPRINTF("Error: Invalid BAR2 window configuration. Offset=0x%"PRIx64", Size=0x%"PRIx64"\n",
                                        set_window_req->offset, set_window_req->size);
                    }
                    // Write response to bar0 mailbox
                    memcpy(s->bar0_mailbox, &set_window_resp, sizeof(set_window_resp));
                    ipc_ret = 0; // No IPC request needed, handled locally
                    tmp_status = set_window_resp.status;
                    expected_server_resp_len = 0; // no server payload
                    break;
                default:
                    CXL_SWITCH_DPRINTF("Error: Unknown command type 0x%02x.\n", cmd_type);
                    qemu_mutex_lock(&s->lock);
                    s->command_status_reg = CMD_STATUS_ERROR_INTERNAL;
                    goto cmd_done_unlock_no_msi;
            }
        }
        
        qemu_mutex_lock(&s->lock);
        if (ipc_ret == 0) {
            if (expected_server_resp_len > 0 && expected_server_resp_len <= BAR0_MAILBOX_SIZE) {
                memcpy(s->bar0_mailbox, ipc_server_resp_payload, expected_server_resp_len);
            } else if (expected_server_resp_len > BAR0_MAILBOX_SIZE) {
                CXL_SWITCH_DPRINTF("Error: Server response size %zu exceeds mailbox size %u.\n", expected_server_resp_len, BAR0_MAILBOX_SIZE);
                s->command_status_reg = CMD_STATUS_ERROR_INTERNAL;
                goto cmd_done_unlock_no_msi;
            }
            // Assume status is 2nd byte
            tmp_status = ((uint8_t *)ipc_server_resp_payload)[1];
            if (tmp_status == CXL_IPC_STATUS_OK) {
                s->command_status_reg = CMD_STATUS_RESPONSE_READY;
                s->interrupt_status_reg |= IRQ_SOURCE_CMD_RESPONSE_READY;
                triggers_msi = true;
            } else {
                s->command_status_reg = CMD_STATUS_ERROR_SERVER;
            }
        } else {
            s->command_status_reg = CMD_STATUS_ERROR_IPC;
            goto cmd_done_unlock_no_msi;
        }
        CXL_SWITCH_DPRINTF("Info: Command 0x%02x completed. Server status was 0x%02x. BAR1 Status updated to 0x%08x.\n", cmd_type, tmp_status, s->command_status_reg);
        break; // REG_COMMAND_DOORBELL case
    case REG_NOTIF_STATUS:
        CXL_SWITCH_DPRINTF("Info: Writing to notification status with value=0x%08"PRIx64". Current notif_status=0x%x\n", val, s->notif_status_reg);
        if ((s->notif_status_reg & NOTIF_STATUS_NEW_CLIENT) && (s->interrupt_status_reg & IRQ_SOURCE_NEW_CLIENT_NOTIFY)) {
            // Clear the notify status
            s->interrupt_status_reg &= ~IRQ_SOURCE_NEW_CLIENT_NOTIFY; 
        }
        s->notif_status_reg = NOTIF_STATUS_NONE;
        break;
    case REG_INTERRUPT_MASK:
        CXL_SWITCH_DPRINTF("Info: Writing to interrupt mask with value=0x%08"PRIx64". Current mask=0x%x\n", val, s->interrupt_mask_reg);
        s->interrupt_mask_reg = (uint32_t)val;
        break;
    case REG_INTERRUPT_STATUS:
        CXL_SWITCH_DPRINTF("Info: Writing to interrupt status with value=0x%08"PRIx64". Current status=0x%x\n", val, s->interrupt_status_reg);
        // Clear bits written to by guest
        s->interrupt_status_reg &= ~((uint32_t)val);
        break;
    default:
        CXL_SWITCH_DPRINTF("Error: Invalid address 0x%"PRIx64" for BAR1 control write.\n", addr);
        break;
    }
cmd_done_unlock_no_msi:
    if (triggers_msi) {
        trigger_msi(s);
    }
    qemu_mutex_unlock(&s->lock);
}

// --- BAR2 Replicated Memory Operations ---

static uint64_t bar2_data_window_read(void *opaque, hwaddr addr, unsigned size)
{
    CXLSwitchClientState *s = opaque;
    uint64_t data = ~0ULL; // Default error value (all FFs)

    if (s->bar2_data_window_size == 0) {
        CXL_SWITCH_DPRINTF("Error: BAR2 data window not configured.\n");
        return data;
    }

    if ((addr + size) > s->bar2_data_window_size) {
        CXL_SWITCH_DPRINTF("GuestError: Read out of bounds (offset=0x%"PRIx64", size=%u, limit=0x%"PRIx64")\n",
                      addr, size, s->bar2_data_window_size);
        return data;
    }
    
    if (s->server_fd < 0) {
        CXL_SWITCH_DPRINTF("Error: Server socket not initialized.\n");
        return data;
    }

    hwaddr addr_in_pool = s->bar2_data_window_offset + addr;
    cxl_ipc_read_req_t read_req = {
        .type = CXL_MSG_TYPE_READ_REQ,
        .addr = addr_in_pool,
        .size = (uint8_t) size,
    };
    cxl_ipc_read_resp_t read_resp;

    CXL_SWITCH_DPRINTF("Info: Sending read request to server (offset=0x%"PRIx64", size=%u)\n",
                  addr, size);
    
    if (cxl_switch_client_ipc_request_response(s, &read_req, sizeof(read_req), &read_resp, sizeof(read_resp)) != 0) {
        CXL_SWITCH_DPRINTF("Error: IPC request failed.\n");
        return data;
    }

    if (read_resp.type != CXL_MSG_TYPE_READ_RESP || read_resp.status != CXL_IPC_STATUS_OK) {
        CXL_SWITCH_DPRINTF("READ server error: Type=0x%02x, Status=0x%02x, Addr=0x%"PRIx64", Size=%u\n",
                           read_resp.type, read_resp.status, addr, size);
        return data;
    }

    CXL_SWITCH_DPRINTF("Info: Read response from server: Type=0x%02x, Status=0x%02x, Addr=0x%"PRIx64", Size=%u, Value=0x%"PRIx64"\n",
                  read_resp.type, read_resp.status, addr, size, read_resp.value);
    return read_resp.value;
}

static void bar2_data_window_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    CXLSwitchClientState *s = opaque;

    if (s->bar2_data_window_size == 0) {
        CXL_SWITCH_DPRINTF("Error: BAR2 data window not configured.\n");
        return;
    }

    if ((addr + size) > s->bar2_data_window_size) {
        CXL_SWITCH_DPRINTF("GuestError: Write out of bounds (offset=0x%"PRIx64", size=%u, limit=0x%"PRIx64")\n",
                      addr, size, s->bar2_data_window_size);
        return;
    }

    if (s->server_fd < 0) {
        CXL_SWITCH_DPRINTF("Error: Server socket not initialized.\n");
        return;
    }

    hwaddr addr_in_pool = s->bar2_data_window_offset + addr;
    cxl_ipc_write_req_t write_req = {
        .type  = CXL_MSG_TYPE_WRITE_REQ,
        .addr  = addr_in_pool,
        .size  = (uint8_t) size,
        .value = val
    };
    cxl_ipc_write_resp_t write_resp;

    CXL_SWITCH_DPRINTF("Info: Sending write request to server (offset=0x%"PRIx64", size=%u, value=0x%"PRIx64")\n",
                  addr, size, val);
    
    if (cxl_switch_client_ipc_request_response(s, &write_req, sizeof(write_req), &write_resp, sizeof(write_resp)) != 0) {
        CXL_SWITCH_DPRINTF("Error: IPC request failed.\n");
        return;
    }

    if (write_resp.type != CXL_MSG_TYPE_WRITE_RESP || write_resp.status != CXL_IPC_STATUS_OK) {
        CXL_SWITCH_DPRINTF("WRITE server error: Type=0x%02x, Status=0x%02x, Addr=0x%"PRIx64", Size=%u\n",
                           write_resp.type, write_resp.status, addr, size);
        return;
    }

    CXL_SWITCH_DPRINTF("Info: Write response from server: Type=0x%02x, Status=0x%02x, Addr=0x%"PRIx64", Size=%u\n",
                  write_resp.type, write_resp.status, addr, size);
}

// --- MSI Trigger ---

static void trigger_msi(CXLSwitchClientState *s)
{
    uint32_t active_and_masked_interrupts = s->interrupt_status_reg & s->interrupt_mask_reg;

    if (active_and_masked_interrupts != 0) {
        // Send a message for vector 0
        // The guest driver: cxl_switch_driver will read REG_INTERRUPT_STATUS
        // to see what caused it.
        CXL_SWITCH_DPRINTF("Info: Triggering MSI vector 0 (IRQ status=0x%x, mask=0x%x)\n",
                      s->interrupt_status_reg, s->interrupt_mask_reg);
        msi_notify(&s->pdev, 0);
        // An MSI is edge-triggered
        // This code assumes that:
        // our guest driver reads, and is also responsible for clearing the
        // source bits in REG_INTERRUPT_STATUS by writing to it, to prevent
        // retriggering the same event.
    }
}

// --- async server fd read handler ---

static void cxl_server_fd_read_handler(void *opaque)
{
    CXLSwitchClientState *s = opaque;
    uint8_t msg_type_header;
    ssize_t n;
    // only call the msi if really necc, which we determine after reading
    bool triggers_msi = false;

    qemu_mutex_lock(&s->lock);
    if (s->server_fd < 0) {
        CXL_SWITCH_DPRINTF("Error: Server socket not initialized.\n");
        goto end;
    }

    n = recv(s->server_fd, &msg_type_header, sizeof(msg_type_header), MSG_PEEK | MSG_DONTWAIT);
    if (n < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            CXL_SWITCH_DPRINTF("Error: Failed to peek at server socket: %s\n", strerror(errno));
            // Close the handler else there has been some srs issue
            qemu_set_fd_handler(s->server_fd, NULL, NULL, NULL);
            close(s->server_fd);
            s->server_fd = -1;
        }
        // No data available which is ok
        goto end;
    } else if (n == 0) {
        // Server closed the connection
        CXL_SWITCH_DPRINTF("Info: Server socket closed by server.\n");
        qemu_set_fd_handler(s->server_fd, NULL, NULL, NULL);
        close(s->server_fd);
        s->server_fd = -1;
        goto end;
    }
    // n > 0: msg available
    if (msg_type_header == CXL_MSG_TYPE_RPC_NEW_CLIENT_NOTIFY) {
        cxl_ipc_rpc_new_client_notify_t notify_payload;
        n = recv(s->server_fd, &notify_payload, sizeof(notify_payload), MSG_WAITALL);
        if (n == sizeof(notify_payload)) {
            CXL_SWITCH_DPRINTF("Info: Received NEW_CLIENT_NOTIFY for service '%s' from client '%s'.\n", notify_payload.service_name, notify_payload.client_instance_id);
            // Copy to bar0 mailbox then set notification
            memcpy(s->bar0_mailbox, &notify_payload, sizeof(notify_payload));
            s->notif_status_reg = NOTIF_STATUS_NEW_CLIENT;
            s->interrupt_status_reg |= IRQ_SOURCE_NEW_CLIENT_NOTIFY;
            triggers_msi = true;
        } else {
            CXL_SWITCH_DPRINTF("Error: Failed to read NEW_CLIENT_NOTIFY payload. Expected %zu bytes, got %zd.\n", sizeof(notify_payload), n);
            // Close the handler else there has been some srs issue
            qemu_set_fd_handler(s->server_fd, NULL, NULL, NULL);
            close(s->server_fd);
            s->server_fd = -1;
            goto end;
        }
    } else {
        CXL_SWITCH_DPRINTF("Error: Unexpected message type header 0x%02x from server.\n", msg_type_header);
        // Do not close handler, just log the error and drain the bit
        char dummy_buf[1024];
        recv(s->server_fd, dummy_buf, sizeof(dummy_buf), MSG_DONTWAIT);
    }
end:
    if (triggers_msi) {
        trigger_msi(s);
    }
    qemu_mutex_unlock(&s->lock);
}


// --- PCI Device Lifecycle ---

static void pci_cxl_switch_client_realize(PCIDevice *pdev, Error **errp)
{
    CXLSwitchClientState *s = CXL_SWITCH_CLIENT(pdev);
    struct sockaddr_un addr;

    s->server_fd = -1;
    s->total_pool_size = 0;
    s->command_status_reg = CMD_STATUS_IDLE;
    s->notif_status_reg = NOTIF_STATUS_NONE;
    s->interrupt_mask_reg = 0; // All interrupts are masked by default
    s->interrupt_status_reg = 0; // No interrupts pending
    memset(s->bar0_mailbox, 0, BAR0_MAILBOX_SIZE);
    s->bar2_data_window_offset = 0;
    s->bar2_data_window_size = BAR2_DATA_SIZE;
    s->bar2_data_size = BAR2_DATA_SIZE; // Default size, can be overridden by QOM property

    qemu_mutex_init(&s->lock);

    // Try to init MSI, if we can't, terminate
    Error *msi_err = NULL;
    if (msi_init(pdev, 0, 1, true, false, &msi_err) != 0) {
        error_propagate(errp, msi_err);
        CXL_SWITCH_DPRINTF("Error: Failed to initialize MSI: %s\n", error_get_pretty(msi_err));
        goto err_destroy_mutex;
    }
    CXL_SWITCH_DPRINTF("Info: MSI initialized successfully for device %s.\n", object_get_canonical_path_component(OBJECT(s)));

    // Set up server fd
    if (!s->server_socket_path) {
        s->server_socket_path = g_strdup(CXL_SWITCH_SERVER_SOCKET_PATH_DEFAULT);
        CXL_SWITCH_DPRINTF("Info: Using default server socket path: %s\n", s->server_socket_path);
    }

    s->server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s->server_fd < 0) {
        error_setg(errp, "CXL Switch (%s): Failed to create socket: %s", object_get_canonical_path_component(OBJECT(s)), strerror(errno));
        goto err_msi_uninit;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, s->server_socket_path, sizeof(addr.sun_path) - 1);

    CXL_SWITCH_DPRINTF("Info: Connecting to server socket: %s\n", addr.sun_path);
    if (connect(s->server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        error_setg(errp, "CXL Switch (%s): Failed to connect to server socket: %s", object_get_canonical_path_component(OBJECT(s)), strerror(errno));
        goto err_close_server_fd;
    }
    CXL_SWITCH_DPRINTF("Info: Connected to server socket %d successfully.\n", s->server_fd);

    qemu_set_fd_handler(s->server_fd, cxl_server_fd_read_handler, NULL, s);

    // Send a request to get the memory size
    cxl_ipc_get_mem_size_req_t mem_size_req = {
        .type = CXL_MSG_TYPE_GET_MEM_SIZE_REQ,
    };
    cxl_ipc_get_mem_size_resp_t mem_size_resp;

    if (cxl_switch_client_ipc_request_response(s, &mem_size_req, sizeof(mem_size_req), &mem_size_resp, sizeof(mem_size_resp)) != 0) {
        CXL_SWITCH_DPRINTF("Error: IPC request failed.\n");
        goto err_remove_fd_handler;
    }

    if (mem_size_resp.type != CXL_MSG_TYPE_GET_MEM_SIZE_RESP || mem_size_resp.status != CXL_IPC_STATUS_OK) {
        CXL_SWITCH_DPRINTF("GET_MEM_SIZE server error: Type=0x%02x, Status=0x%02x\n",
                           mem_size_resp.type, mem_size_resp.status);
        goto err_remove_fd_handler;
    }

    s->total_pool_size = mem_size_resp.mem_size;
    CXL_SWITCH_DPRINTF("Info: Memory size from server: %"PRIu64" bytes\n", s->total_pool_size);

    // Init all the Bars
    // Bar0: Management mailbox
    memory_region_init_io(&s->bar0_mailbox_region, OBJECT(s), &bar0_mailbox_ops, s, "cxl-switch-client-bar0-mailbox", BAR0_MAILBOX_SIZE);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY | PCI_BASE_ADDRESS_MEM_TYPE_32, &s->bar0_mailbox_region);
    CXL_SWITCH_DPRINTF("Info: BAR0 (mailbox) registered, size %u bytes.\n", BAR0_MAILBOX_SIZE);

    // Bar1: Control registers
    memory_region_init_io(&s->bar1_control_region, OBJECT(s), &bar1_control_ops, s, "cxl-switch-client-bar1-control", BAR1_CONTROL_SIZE);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY | PCI_BASE_ADDRESS_MEM_TYPE_32, &s->bar1_control_region);
    CXL_SWITCH_DPRINTF("Info: BAR1 (control) registered, size %u bytes.\n", BAR1_CONTROL_SIZE);

    // Bar2: Data region
    memory_region_init_io(&s->bar2_data_region, OBJECT(s), &bar2_data_ops, s, "cxl-switch-client-bar2-data", s->bar2_data_size);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY | PCI_BASE_ADDRESS_MEM_TYPE_64 | PCI_BASE_ADDRESS_MEM_PREFETCH, &s->bar2_data_region);
    CXL_SWITCH_DPRINTF("Info: BAR2 (data) registered, size %"PRIu64" bytes.\n", s->bar2_data_size);

    CXL_SWITCH_DPRINTF("Info: CXL Switch Client (%s) realized successfully.\n",
                  object_get_canonical_path_component(OBJECT(s)));
    return;
err_remove_fd_handler:
    qemu_set_fd_handler(s->server_fd, NULL, NULL, NULL);
err_close_server_fd:
    if (s->server_fd >= 0) close(s->server_fd);
    s->server_fd = -1;
err_msi_uninit:
    msi_uninit(pdev);
err_destroy_mutex:
    qemu_mutex_destroy(&s->lock);
    g_free(s->server_socket_path);
    s->server_socket_path = NULL;
    CXL_SWITCH_DPRINTF("Error: Failed to realize CXL Switch Client (%s)\n",
                  object_get_canonical_path_component(OBJECT(s)));
}

static void pci_cxl_switch_client_uninit(PCIDevice *pdev)
{
    CXLSwitchClientState *s = CXL_SWITCH_CLIENT(pdev);
    CXL_SWITCH_DPRINTF("Info: Uninitializing device %s.\n", object_get_canonical_path_component(OBJECT(s)));

    if (s->server_fd >= 0) {
        CXL_SWITCH_DPRINTF("Info: Closing server socket %d.\n", s->server_fd);
        close(s->server_fd);
        s->server_fd = -1;
    }

    msi_uninit(pdev);
    qemu_mutex_destroy(&s->lock);
    g_free(s->server_socket_path);
}

/** QOM Type Registration */
static Property cxl_switch_client_properties[] = {
    DEFINE_PROP_STRING("socket-path", CXLSwitchClientState, server_socket_path),
};

static void cxl_switch_client_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pci_cxl_switch_client_realize;
    k->exit = pci_cxl_switch_client_uninit;
    k->vendor_id = PCI_VENDOR_ID_QEMU_CXL_SWITCH;
    k->device_id = PCI_CXL_DEVICE_ID;
    k->class_id = PCI_CLASS_MEMORY_RAM;
    k->revision = 2;

    device_class_set_props(dc, cxl_switch_client_properties);
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->desc = "CXL Switch Client (connects to external CXL Switch Server)";
}

static void cxl_switch_client_instance_init(Object *obj)
{
    CXLSwitchClientState *s = CXL_SWITCH_CLIENT(obj);
    s->server_socket_path = NULL; // Will be set by QOM property or be defaulted
    s->server_fd = -1;
    s->total_pool_size = 0;
    s->command_status_reg = CMD_STATUS_IDLE;
    s->notif_status_reg = NOTIF_STATUS_NONE;
    s->interrupt_mask_reg = 0; // All interrupts are masked by default
    s->interrupt_status_reg = 0; // No interrupts pending
    memset(s->bar0_mailbox, 0, BAR0_MAILBOX_SIZE);
    s->bar2_data_size = BAR2_DATA_SIZE;
    s->bar2_data_window_offset = 0;
    s->bar2_data_window_size = 0;
}

static InterfaceInfo interfaces[] = {
    { INTERFACE_CONVENTIONAL_PCI_DEVICE },
    { },
};

static const TypeInfo cxl_switch_client_info = {
    .name = TYPE_PCI_CXL_SWITCH_CLIENT,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(CXLSwitchClientState),
    .instance_init = cxl_switch_client_instance_init,
    .class_init = cxl_switch_client_class_init,
    .interfaces = interfaces,
};

static void pci_cxl_switch_client_register_types(void)
{
    type_register_static(&cxl_switch_client_info);
}

type_init(pci_cxl_switch_client_register_types);