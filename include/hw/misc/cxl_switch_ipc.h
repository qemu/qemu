#ifndef CXL_SWITCH_IPC_H
#define CXL_SWITCH_IPC_H

#include <stdint.h>
#include <stddef.h>

#define CXL_SWITCH_SERVER_SOCKET_PATH_DEFAULT "/tmp/cxl_switch_server.sock"

// Message types
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

// Status codes
typedef enum {
    CXL_IPC_STATUS_OK = 0x00,
    CXL_IPC_STATUS_ERROR_GENERIC = 0x01,
    CXL_IPC_STATUS_ERROR_INVALID_REQ = 0x02,
    CXL_IPC_STATUS_ERROR_IO = 0x03,
    CXL_IPC_STATUS_ERROR_NO_HEALTHY_BACKEND = 0x04,
    CXL_IPC_STATUS_ERROR_OUT_OF_BOUNDS = 0x05,
} cxl_ipc_status_t;

// Common header for all messages.
// For now, we use distinct structs for each message types for clarity

// --- Message structures ---
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

#endif