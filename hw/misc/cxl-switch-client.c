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

typedef struct CXLSwitchClientState CXLSwitchClientState;
DECLARE_INSTANCE_CHECKER(CXLSwitchClientState, CXL_SWITCH_CLIENT, TYPE_PCI_CXL_SWITCH_CLIENT)

#define PCI_VENDOR_ID_QEMU_CXL_SWITCH 0x1AF4
#define PCI_CXL_DEVICE_ID 0x1337

struct CXLSwitchClientState {
    PCIDevice pdev;

    MemoryRegion replicated_mr_bar;  // BAR2: Guest access

    char *server_socket_path;  // QOM property
    int server_fd;
    uint64_t mem_size; // Size of the replicated memory region

    QemuMutex lock; // Serialize access to server_fd from MMIO callbacks
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

/* BAR2 Replicated Memory Operations */

static uint64_t cxl_switch_client_mem_read(void *opaque, hwaddr addr, unsigned size)
{
    CXLSwitchClientState *s = opaque;
    uint64_t data = ~0ULL; // Default error value (all FFs)
    
    if (s->server_fd < 0) {
        CXL_SWITCH_DPRINTF("Error: Server socket not initialized.\n");
        return data;
    }

    if ((addr + size) > s->mem_size) {
        CXL_SWITCH_DPRINTF("GuestError: Read out of bounds (offset=0x%"PRIx64", size=%u, limit=0x%"PRIx64")\n",
                      addr, size, s->mem_size);
        return data;
    }

    cxl_ipc_read_req_t read_req = {
        .type = CXL_MSG_TYPE_READ_REQ,
        .addr = addr,
        .size = size,
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

static void cxl_switch_client_mem_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    CXLSwitchClientState *s = opaque;

    if (s->server_fd < 0) {
        CXL_SWITCH_DPRINTF("Error: Server socket not initialized.\n");
        return;
    }

    if ((addr + size) > s->mem_size) {
        CXL_SWITCH_DPRINTF("GuestError: Write out of bounds (offset=0x%"PRIx64", size=%u, limit=0x%"PRIx64")\n",
                      addr, size, s->mem_size);
        return;
    }

    cxl_ipc_write_req_t write_req = {
        .type  = CXL_MSG_TYPE_WRITE_REQ,
        .addr  = addr,
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

static const MemoryRegionOps cxl_switch_mem_ops = {
    .read = cxl_switch_client_mem_read,
    .write = cxl_switch_client_mem_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
};

/* PCI Device Lifecycle */

static void pci_cxl_switch_client_realize(PCIDevice *pdev, Error **errp)
{
    CXLSwitchClientState *s = CXL_SWITCH_CLIENT(pdev);
    CXL_SWITCH_DPRINTF("Info: Realizing device with mem-size %"PRIu64".\n", s->mem_size); // Added device ID manually
    struct sockaddr_un addr;

    s->server_fd = -1;
    s->mem_size = 0;
    qemu_mutex_init(&s->lock);

    if (!s->server_socket_path) {
        s->server_socket_path = g_strdup(CXL_SWITCH_SERVER_SOCKET_PATH_DEFAULT);
        CXL_SWITCH_DPRINTF("Info: Using default server socket path: %s\n", s->server_socket_path);
    }

    s->server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s->server_fd < 0) {
        error_setg(errp, "CXL Switch (%s): Failed to create socket: %s", object_get_canonical_path_component(OBJECT(s)), strerror(errno));
        return;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, s->server_socket_path, sizeof(addr.sun_path) - 1);

    CXL_SWITCH_DPRINTF("Info: Connecting to server socket: %s\n", addr.sun_path);
    if (connect(s->server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        error_setg(errp, "CXL Switch (%s): Failed to connect to server socket: %s", object_get_canonical_path_component(OBJECT(s)), strerror(errno));
        close(s->server_fd);
        s->server_fd = -1;
        return;
    }
    CXL_SWITCH_DPRINTF("Info: Connected to server socket %d successfully.\n", s->server_fd);

    // Send a request to get the memory size
    cxl_ipc_get_mem_size_req_t mem_size_req = {
        .type = CXL_MSG_TYPE_GET_MEM_SIZE_REQ,
    };
    cxl_ipc_get_mem_size_resp_t mem_size_resp;

    if (cxl_switch_client_ipc_request_response(s, &mem_size_req, sizeof(mem_size_req), &mem_size_resp, sizeof(mem_size_resp)) != 0) {
        CXL_SWITCH_DPRINTF("Error: IPC request failed.\n");
        close(s->server_fd);
        s->server_fd = -1;
        return;
    }

    if (mem_size_resp.type != CXL_MSG_TYPE_GET_MEM_SIZE_RESP || mem_size_resp.status != CXL_IPC_STATUS_OK) {
        CXL_SWITCH_DPRINTF("GET_MEM_SIZE server error: Type=0x%02x, Status=0x%02x\n",
                           mem_size_resp.type, mem_size_resp.status);
        close(s->server_fd);
        s->server_fd = -1;
        return;
    }

    s->mem_size = mem_size_resp.mem_size;
    CXL_SWITCH_DPRINTF("Info: Memory size from server: %"PRIu64" bytes\n", s->mem_size);

    // Initialize the replicated memory region
    memory_region_init_io(&s->replicated_mr_bar, OBJECT(s), &cxl_switch_mem_ops, s, "cxl-switch-replicated-mem", s->mem_size);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY | PCI_BASE_ADDRESS_MEM_TYPE_64 | PCI_BASE_ADDRESS_MEM_PREFETCH, &s->replicated_mr_bar);

    CXL_SWITCH_DPRINTF("Info: BAR0 (effectively BAR2) registered for replication, size %"PRIu64".\n", s->mem_size);
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
    s->mem_size = 0;
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