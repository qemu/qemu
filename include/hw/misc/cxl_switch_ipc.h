#ifndef CXL_SWITCH_IPC_H
#define CXL_SWITCH_IPC_H

#include <stdint.h>
#include <stddef.h>

#define CXL_SWITCH_SERVER_SOCKET_PATH_DEFAULT "/tmp/cxl_switch_server.sock"
#define CXL_SWITCH_SERVER_ADMIN_SOCKET_PATH_DEFAULT "/tmp/cxl_switch_server_admin.sock"

#define MAX_SERVICE_NAME_LEN    64
#define MAX_INSTANCE_ID_LEN     64
#define MAX_ENDPOINT_PATH_LEN  128

// Message types for QEMU Client <-> Server
typedef enum {
    CXL_MSG_TYPE_CONNECT_REQ = 0x00,
    CXL_MSG_TYPE_CONNECT_RESP = 0x01,
    CXL_MSG_TYPE_GET_MEM_SIZE_REQ = 0x02,
    CXL_MSG_TYPE_GET_MEM_SIZE_RESP = 0x03,
    CXL_MSG_TYPE_READ_REQ = 0x04,
    CXL_MSG_TYPE_READ_RESP = 0x05,
    CXL_MSG_TYPE_WRITE_REQ = 0x06,
    CXL_MSG_TYPE_WRITE_RESP = 0x07,
    CXL_MSG_TYPE_ERROR_RESP = 0xFF,
} cxl_ipc_msg_type_t;

// RPC Management and QEMU Device Control (RPC app/lib <-> QEMU Device Mailbox)
// Some are forwarded from QEMU Client to CXL Server, others are handled by QEMU Device directly
typedef enum {
    // Commands forwarded to server
    CXL_MSG_TYPE_RPC_REGISTER_SERVICE_REQ = 0x20,
    CXL_MSG_TYPE_RPC_REGISTER_SERVICE_RESP = 0x21,
    CXL_MSG_TYPE_RPC_REQUEST_CHANNEL_REQ = 0x22,
    CXL_MSG_TYPE_RPC_REQUEST_CHANNEL_RESP = 0x23,
    CXL_MSG_TYPE_RPC_RELEASE_CHANNEL_REQ = 0x24,
    CXL_MSG_TYPE_RPC_RELEASE_CHANNEL_RESP = 0x25,
    CXL_MSG_TYPE_RPC_NEW_CLIENT_NOTIFY = 0x26, // FM -> Server notif
    CXL_MSG_TYPE_RPC_DEREGISTER_SERVICE_REQ = 0x27,
    CXL_MSG_TYPE_RPC_DEREGISTER_SERVICE_RESP = 0x28,
    // Commands handled by QEMU device locally (to configure BAR2)
    CXL_MSG_TYPE_RPC_SET_BAR2_WINDOW_REQ = 0x29,
    CXL_MSG_TYPE_RPC_SET_BAR2_WINDOW_RESP = 0x30,
    // Generic error for mgmt
    CXL_MSG_TYPE_RPC_MGMT_ERROR_RESP = 0x3F,
} cxl_ipc_rpc_mgmt_msg_type_t;

// Admin message types (Host Tool <-> CXL Server)
typedef enum {
    CXL_ADMIN_CMD_TYPE_FAIL_REPLICA = 0xA1,
    CXL_ADMIN_CMD_TYPE_RECOVER_REPLICA = 0xA2,
    CXL_ADMIN_CMD_TYPE_GET_REPLICA_STATUS = 0xA3,
} cxl_admin_cmd_type_t;

// Status codes
typedef enum {
    CXL_IPC_STATUS_OK = 0x00,
    CXL_IPC_STATUS_ERROR_GENERIC = 0x01,
    CXL_IPC_STATUS_ERROR_INVALID_REQ = 0x02,
    CXL_IPC_STATUS_ERROR_IO = 0x03,
    CXL_IPC_STATUS_ERROR_NO_HEALTHY_BACKEND = 0x04,
    CXL_IPC_STATUS_ERROR_OUT_OF_BOUNDS = 0x05,

    CXL_IPC_STATUS_SERVICE_NOT_FOUND = 0x06,
    CXL_IPC_STATUS_REGISTRATION_FAILED = 0x07,
    CXL_IPC_STATUS_CHANNEL_ALLOC_FAILED = 0x08,
    CXL_IPC_STATUS_SERVER_UNAVAILABLE = 0x09,
    CXL_IPC_STATUS_NOTIFICATION_FAILED = 0x0A,
    CXL_IPC_STATUS_BAR2_FAILED = 0x0B,
} cxl_ipc_status_t;

// Common header for all messages.
// For now, we use distinct structs for each message types for clarity

// --- Message structures for QEMU Client <-> Server ---
// CXL_MSG_TYPE_GET_MEM_SIZE_REQ
typedef struct {
    uint8_t type; // CXL_MSG_TYPE_GET_MEM_SIZE_REQ
} cxl_ipc_get_mem_size_req_t;

// CXL_MSG_TYPE_GET_MEM_SIZE_RESP
typedef struct {
    uint8_t type;     // CXL_MSG_TYPE_GET_MEM_SIZE_RESP
    uint8_t status;   // cxl_ipc_status_t
    uint64_t mem_size; // Total size of the replicated memory region
} cxl_ipc_get_mem_size_resp_t;

// CXL_MSG_TYPE_WRITE_REQ
typedef struct {
    uint8_t type;    // CXL_MSG_TYPE_WRITE_REQ
    uint64_t channel_id; // RPC Channel identifier: identifies which mem replicas
    uint64_t addr;   // Address within the replicated memory
    uint8_t size;    // Access size (1, 2, 4, 8)
    uint64_t value;  // Value to write
} cxl_ipc_write_req_t;

// CXL_MSG_TYPE_WRITE_RESP
typedef struct {
    uint8_t type;   // CXL_MSG_TYPE_WRITE_RESP
    uint8_t status; // cxl_ipc_status_t
} cxl_ipc_write_resp_t;

// CXL_MSG_TYPE_READ_REQ
typedef struct {
    uint8_t type;   // CXL_MSG_TYPE_READ_REQ
    uint64_t channel_id; // RPC Channel identifier: identifies which mem replicas
    uint64_t addr;  // Address within the replicated memory
    uint8_t size;   // Access size (1, 2, 4, 8)
} cxl_ipc_read_req_t;

// CXL_MSG_TYPE_READ_RESP
typedef struct {
    uint8_t type;   // CXL_MSG_TYPE_READ_RESP
    uint8_t status; // cxl_ipc_status_t
    uint64_t value; // Value read (if status is OK)
} cxl_ipc_read_resp_t;

// CXL_MSG_TYPE_ERROR_RESP (Generic error if type-specific response not suitable)
typedef struct {
    uint8_t type;   // CXL_MSG_TYPE_ERROR_RESP
    uint8_t status; // cxl_ipc_status_t (specific error code)
} cxl_ipc_error_resp_t;

// --- Admin Message Types ---
// Admin Command
typedef struct {
    uint8_t cmd_type;
    uint8_t memdev_index;
} cxl_admin_fail_replica_req_t;

// Admin response
typedef struct {
    uint8_t status;
} cxl_admin_fail_replica_resp_t;

// --- RPC Management and QEMU Device Control Messages ---
// CXL_MSG_TYPE_RPC_REGISTER_SERVICE
typedef struct {
    uint8_t type;
    char service_name[MAX_SERVICE_NAME_LEN];
    char instance_id[MAX_INSTANCE_ID_LEN];
} cxl_ipc_rpc_register_service_req_t;

typedef struct {
    uint8_t type;
    uint8_t status;
} cxl_ipc_rpc_register_service_resp_t;

// CXL_MSG_TYPE_RPC_DEREGISTER_SERVICE
typedef struct {
    uint8_t type;
    char service_name[MAX_SERVICE_NAME_LEN];
    char instance_id[MAX_INSTANCE_ID_LEN];
} cxl_ipc_rpc_deregister_service_req_t;

typedef struct {
    uint8_t type;
    uint8_t status;
} cxl_ipc_rpc_deregister_service_resp_t;

// CXL_MSG_TYPE_RPC_REQUEST_CHANNEL
typedef struct {
    uint8_t type;
    char service_name[MAX_SERVICE_NAME_LEN];
    char instance_id[MAX_INSTANCE_ID_LEN];
} cxl_ipc_rpc_request_channel_req_t;

typedef struct {
    uint8_t type;
    uint8_t status;
    uint64_t channel_shm_offset;
    uint32_t channel_shm_size;
    uint64_t channel_id;
} cxl_ipc_rpc_request_channel_resp_t;

// CXL_MSG_TAYPE_RPC_RELEASE_CHANNEL
typedef struct {
    uint8_t type;
    uint64_t channel_id;
} cxl_ipc_rpc_release_channel_req_t;

typedef struct {
    uint8_t type;
    uint8_t status;
} cxl_ipc_rpc_release_channel_resp_t;

// CXL_MSG_TYPE_RPC_NEW_CLIENT_NOTIFY
// (Server -> QEMU Device -> BAR0 for Server library)
typedef struct {
    uint8_t type;
    uint64_t channel_shm_offset;
    uint32_t channel_shm_size;
    uint64_t channel_id;
    char client_instance_id[MAX_INSTANCE_ID_LEN];
    char service_name[MAX_SERVICE_NAME_LEN];
} cxl_ipc_rpc_new_client_notify_t;

// CXL_MSG_TYPE_RPC_CONFIG_BAR2_WINDOW_REQ
// Guest lib <-> QEMU Device mailbox
typedef struct {
    uint8_t type; 
    uint64_t offset; 
    uint64_t size; 
} cxl_ipc_rpc_set_bar2_window_req_t;

typedef struct {
    uint8_t type; 
    uint8_t status;
} cxl_ipc_rpc_set_bar2_window_resp_t;

// Generic error for RPC management
typedef struct {
    uint8_t type; 
    uint8_t status;
} cxl_ipc_rpc_mgmt_error_resp_t;

#endif