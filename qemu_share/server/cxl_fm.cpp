/**
  CXL Switch Fabric Manager emulator
  This behaves similarly to the standard FM in a CXL switch
  It handles registration/deregistration of CXL memory devices
  It assigns mem regions from mem devices to clients/servers
*/

#include <bits/types/struct_timeval.h>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <sys/types.h>
#include <system_error>
#include <algorithm>
#include <vector>
#include <cstring>

#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include "cxl_fm.hpp"
#include "memdevice.hpp"
#include "cxl_switch_ipc.h"

namespace cxl_fm {

#define CXL_FM_DEBUG 1
#if CXL_FM_DEBUG
#define CXL_FM_LOG(msg) do { std::cerr << "CXL FM: " << msg << std::endl; } while(0)
#define CXL_FM_LOG_P(msg, val) do { std::cerr << "CXL FM: " << msg << val << std::endl; } while(0)
#else
#define CXL_FM_LOG(msg)
#define CXL_CXL_FM_LOG_P(msg, val)
#endif

// --- Event management ---

//  --- Main Request handlers ---

// Ngl, not sure what this shud be doing anymore
void CXLFabricManager::handle_get_mem_size(int qemu_vm_fd) {
  cxl_ipc_get_mem_size_resp_t resp;
  resp.type = CXL_MSG_TYPE_GET_MEM_SIZE_RESP;
  resp.status = CXL_IPC_STATUS_OK;
  resp.mem_size = config_.replica_mem_size;
  
  CXL_FM_LOG("Sending memory size response, size: " + std::to_string(resp.mem_size) + " bytes");
  ::send(qemu_vm_fd, &resp, sizeof(resp), 0);
}

void CXLFabricManager::handle_write_mem_req(int qemu_vm_fd, const cxl_ipc_write_req_t& req) {
  cxl_ipc_write_resp_t resp;
  resp.type = CXL_MSG_TYPE_WRITE_RESP;
  resp.status = CXL_IPC_STATUS_ERROR_GENERIC;

  CXL_FM_LOG("Received WRITE_REQ" 
             ", channel_id: " + std::to_string(req.channel_id) + 
             ", addr: " + std::to_string(req.addr) +
             ", size: " + std::to_string(req.size) +
             ", value: " + std::to_string(req.value));
  
  // Early terminate from a nonsensical request
  if ((req.addr + req.size) > config_.replica_mem_size) {
    CXL_FM_LOG("Write request out of bounds, addr: " + std::to_string(req.addr) +
               ", size: " + std::to_string(req.size) +
               ", limit: " + std::to_string(config_.replica_mem_size));
    resp.status = CXL_IPC_STATUS_ERROR_OUT_OF_BOUNDS;
    ::send(qemu_vm_fd, &resp, sizeof(resp), 0);
    return;
  }

  auto it = active_rpc_connections_.find(req.channel_id);
  if (it == active_rpc_connections_.end()) {
    // If an RPCConnection is not found,
    // one possibility is the RPConnection was freed
    // and this is an errant request.
    // We should ignore the request
    resp.status = CXL_IPC_STATUS_ERROR_INVALID_REQ;
    send(qemu_vm_fd, &resp, sizeof(resp), 0);
    return;
  }

  RPCConnection& rpc_connection = it->second;

  if (rpc_connection.allocated_regions.empty()) {
    CXL_FM_LOG("RPCConnection has no allocated regions, cannot handle write request.");
    resp.status = CXL_IPC_STATUS_ERROR_NO_HEALTHY_BACKEND;
    ::send(qemu_vm_fd, &resp, sizeof(resp), 0);
    return;
  }

  // TODO: Handle state migration
  int num_successful_writes = 0;

  for (auto& allocated_region : rpc_connection.allocated_regions) {
    CXLMemDevice* device = allocated_region.backing_device;

    if (!device) {
      // TODO: In practice, we should handle this another way
      CXL_FM_LOG("Allocated region has no backing device, skipping write.");
      continue;
    }
    
    try {
      // req.addr is the logical offset within the allocated region
      // allocated_region.offset is the start of this allocated block on
      //                         the CXLMemDevice
      // We handle the check within the write_data and catch any errors
      uint64_t actual_offset = req.addr + allocated_region.offset;

      CXL_FM_LOG("Writing to device: "
                 ", logical_addr: " + std::to_string(req.addr) +
                 ", actual_offset_on_device: " + std::to_string(actual_offset) +
                 ", size: " + std::to_string(req.size) +
                 ", value: " + std::to_string(req.value));

      device->write_data(actual_offset, reinterpret_cast<const void*>(&req.value), req.size);
      num_successful_writes++;
    } catch (const std::out_of_range& oor_ex) {
      CXL_FM_LOG("Write out of bounds: " + std::string(oor_ex.what()) +
                 ", addr: " + std::to_string(req.addr) +
                 ", size: " + std::to_string(req.size));
      // This should not happen...
    } catch (const std::runtime_error& rt_err) {
      CXL_FM_LOG("Runtime error during write: " + std::string(rt_err.what()) +
                 ", addr: " + std::to_string(req.addr) +
                 ", size: " + std::to_string(req.size));
    } catch (const std::exception& e) {
      CXL_FM_LOG("Exception during write: " + std::string(e.what()) +
                 ", addr: " + std::to_string(req.addr) +
                 ", size: " + std::to_string(req.size));
    }
  }

  if (num_successful_writes == rpc_connection.allocated_regions.size()) {
    resp.status = CXL_IPC_STATUS_OK;
    CXL_FM_LOG("Write completely successful, num_successful_writes: " + std::to_string(num_successful_writes));
  } else if (num_successful_writes > 0 && num_successful_writes < rpc_connection.allocated_regions.size()) {
    // Some writes were successful, but not all
    resp.status = CXL_IPC_STATUS_ERROR_IO;
    CXL_FM_LOG("Partial success, num_successful_writes: " + std::to_string(num_successful_writes));
  } else if (num_successful_writes == 0 && !rpc_connection.allocated_regions.empty()) {
    resp.status = CXL_IPC_STATUS_ERROR_IO;
  } else {
    resp.status = CXL_IPC_STATUS_ERROR_GENERIC;
  }

  ::send(qemu_vm_fd, &resp, sizeof(resp), 0);
}

void CXLFabricManager::handle_read_mem_req(int qemu_vm_fd, const cxl_ipc_read_req_t& req) {
  cxl_ipc_read_resp_t resp;
  resp.type = CXL_MSG_TYPE_READ_RESP;
  resp.status = CXL_IPC_STATUS_ERROR_GENERIC;
  resp.value = ~0ULL; // Default value in case of error

  CXL_FM_LOG("Received READ_REQ" 
             ", channel_id: " + std::to_string(req.channel_id) + 
             ", addr: " + std::to_string(req.addr) +
             ", size: " + std::to_string(req.size));
  
  // Early terminate from a nonsensical request
  if ((req.addr + req.size) > config_.replica_mem_size) {
    CXL_FM_LOG("Read request out of bounds, addr: " + std::to_string(req.addr) +
               ", size: " + std::to_string(req.size) +
               ", limit: " + std::to_string(config_.replica_mem_size));
    resp.status = CXL_IPC_STATUS_ERROR_OUT_OF_BOUNDS;
    ::send(qemu_vm_fd, &resp, sizeof(resp), 0);
    return;
  }

  auto it = active_rpc_connections_.find(req.channel_id);
  if (it == active_rpc_connections_.end()) {
    // If an RPCConnection is not found,
    // one possibility is the RPConnection was freed
    // and this is an errant request.
    // We should ignore the request
    resp.status = CXL_IPC_STATUS_ERROR_INVALID_REQ;
    send(qemu_vm_fd, &resp, sizeof(resp), 0);
    return;
  }

  RPCConnection& rpc_connection = it->second;

  if (rpc_connection.allocated_regions.empty()) {
    CXL_FM_LOG("RPCConnection has no allocated regions, cannot handle read request.");
    resp.status = CXL_IPC_STATUS_ERROR_NO_HEALTHY_BACKEND;
    ::send(qemu_vm_fd, &resp, sizeof(resp), 0);
    return;
  }

  for (auto& allocated_region : rpc_connection.allocated_regions) {
    CXLMemDevice* device = allocated_region.backing_device;

    if (!device) {
      // TODO: In practice, we should handle this another way
      CXL_FM_LOG("Allocated region has no backing device, skipping write.");
      continue;
    }
    
    try {
      // req.addr is the logical offset within the allocated region
      // allocated_region.offset is the start of this allocated block on
      //                         the CXLMemDevice
      // We handle the check within the read_data and catch any errors
      uint64_t actual_offset = req.addr + allocated_region.offset;
      // Temp buffer to hold data as max read is 8 bytes
      uint8_t tmp_buffer[8];
      resp.value = 0; // init to all 0s to ensure upper bytes are zero-ed
      
      CXL_FM_LOG("Attempting to read from device: "
                 ", logical_addr: " + std::to_string(req.addr) +
                 ", actual_offset_on_device: " + std::to_string(actual_offset) +
                 ", size: " + std::to_string(req.size));
      // Bounds check performed here
      device->read_data(actual_offset, tmp_buffer, req.size);
      // Copy data from tmp_buffer to resp.value, respecting the desired size
      // using a reinterpret_cast
      switch(req.size) {
      case 1:
        resp.value = *reinterpret_cast<uint8_t*>(tmp_buffer);
        resp.status = CXL_IPC_STATUS_OK;
        break;
        case 2:
        resp.value = *reinterpret_cast<uint16_t*>(tmp_buffer);
        resp.status = CXL_IPC_STATUS_OK;
        break;
        case 4:
        resp.value = *reinterpret_cast<uint32_t*>(tmp_buffer);
        resp.status = CXL_IPC_STATUS_OK;
        break;
        case 8:
        resp.value = *reinterpret_cast<uint64_t*>(tmp_buffer);
        resp.status = CXL_IPC_STATUS_OK;
        break;
      default:
        resp.status = CXL_IPC_STATUS_ERROR_INVALID_REQ;
        break;
      }
    } catch (const std::out_of_range& oor_ex) {
      CXL_FM_LOG("Read out of bounds: " + std::string(oor_ex.what()) +
                 ", addr: " + std::to_string(req.addr) +
                 ", size: " + std::to_string(req.size));
      resp.status = CXL_IPC_STATUS_ERROR_OUT_OF_BOUNDS;
    } catch (const std::runtime_error& rt_err) {
      CXL_FM_LOG("Runtime error during read: " + std::string(rt_err.what()) +
                 ", addr: " + std::to_string(req.addr) +
                 ", size: " + std::to_string(req.size));
      resp.status = CXL_IPC_STATUS_ERROR_IO;
    } catch (const std::exception& e) {
      CXL_FM_LOG("Exception during read: " + std::string(e.what()) +
                 ", addr: " + std::to_string(req.addr) +
                 ", size: " + std::to_string(req.size));
      resp.status = CXL_IPC_STATUS_ERROR_GENERIC;
    }
  }
  // One of the reads should succeed, else there was no healthy backend.
  if (resp.status != CXL_IPC_STATUS_OK) {
    resp.status = CXL_IPC_STATUS_ERROR_NO_HEALTHY_BACKEND;
  }
  ::send(qemu_vm_fd, &resp, sizeof(resp), 0);
}

void CXLFabricManager::handle_register_rpc_service(int qemu_vm_fd, const cxl_ipc_rpc_register_service_req_t& req) {
  cxl_ipc_rpc_register_service_resp_t resp;
  resp.type = CXL_MSG_TYPE_RPC_REGISTER_SERVICE_RESP;
  resp.status = CXL_IPC_STATUS_REGISTRATION_FAILED;

  std::string service_name_str(req.service_name);
  std::string server_id_str(req.instance_id);

  CXL_FM_LOG("RPC_REGISTER_SERVICE_REQ from qemu_fd " + std::to_string(qemu_vm_fd) +
               ": Service='" + service_name_str + "', Instance ID='" + server_id_str + "'");


  auto& server_list = service_registry_[service_name_str];
  server_list.emplace_back(server_id_str, qemu_vm_fd);
  resp.status = CXL_IPC_STATUS_OK;
  ::send(qemu_vm_fd, &resp, sizeof(resp), 0);
}

/**
  This function does many things.
  Any of which can fail independently, and violates the correctness.
  1. Find the RPC service
  2. Find a server to service the RPC connection request
  3. Find 3 mem devices to back up the connection
  4. Create RPC connection struct
  5. Send response to RPC client (on QEMU VM)
  6. Send response to RPC server (on QEMU VM)
*/
void CXLFabricManager::handle_rpc_request_channel_req(int qemu_client_fd, const cxl_ipc_rpc_request_channel_req_t& req) {
  // Payload to the client
  cxl_ipc_rpc_request_channel_resp_t client_resp;
  client_resp.type = CXL_MSG_TYPE_RPC_REQUEST_CHANNEL_RESP;
  client_resp.status = CXL_IPC_STATUS_ERROR_GENERIC;
  client_resp.channel_shm_offset = 0;
  client_resp.channel_shm_size = 0;

  std::string service_name_str(req.service_name);
  std::string client_id_str(req.instance_id);

  RPCServerInstanceInfo chosen_server_info("", -1);
  int qemu_server_fd = -1;

  CXL_FM_LOG("RPC_REQUEST_CHANNEL_REQ from qemu_fd " + std::to_string(qemu_client_fd) +
               ": Service='" + service_name_str);
  
  // 1. Find the RPC service
  auto service_it = service_registry_.find(service_name_str);
  if (service_it == service_registry_.end()) {
    CXL_FM_LOG("RPC service '" + service_name_str + "' not found.");
    client_resp.status = CXL_IPC_STATUS_SERVICE_NOT_FOUND;
    ::send(qemu_client_fd, &client_resp, sizeof(client_resp), 0);
  }

  // 2. Find a server to service the RPC connection request
  if (service_it->second.empty()) {
    CXL_FM_LOG("RPC service '" + service_name_str + "' has no instances registered.");
    client_resp.status = CXL_IPC_STATUS_SERVICE_NOT_FOUND;
    ::send(qemu_client_fd, &client_resp, sizeof(client_resp), 0);
  }

  // Danger here: atm, our QEMU VMs cannot really concurrently service RPCs
  //              but here, we are picking the first server that can service
  //              the RPC! Concurrently handling RPCs is the ideal behavior,
  //              so this line will not change. OTOH, to research on QEMU
  chosen_server_info = service_it->second.front();
  qemu_server_fd = chosen_server_info.qemu_client_fd;

  // 3. Find 3 mem devices to back up the connection
  // TODO: Atm, we do a simple strategy of always picking the first 3 available
  //       ones. Obviously, some form of load balancing should be performed in
  //       the actual impl.

  // TODO: Atm, we also hardcode a request size regardless of the RPC service.
  //       In reality, we want to pick a smart request size based off the RPC.
  //       Atm, we will pick a hardcoded value large enough s.t. this isn't
  //       an issue.
  std::vector<AllocatedRegionInfo> allocated_regions;

  uint32_t requested_size = (256 * 1024 * 1024); // 256 MB
  
  int num_allocated_replicas = 0;
  for (size_t i = 0; i < mem_devices_.size() && num_allocated_replicas < NUM_REPLICAS; i++) {
    std::optional<size_t> allocated_offset = mem_devices_[i].allocate(static_cast<size_t>(requested_size));
    if (allocated_offset.has_value()) {
      AllocatedRegionInfo ar;
      ar.offset = allocated_offset.value();
      ar.size = requested_size;
      ar.backing_device = &mem_devices_[i];
      allocated_regions.push_back(std::move(ar));
      num_allocated_replicas++;
    } else {
      CXL_FM_LOG("Failed to allocate region on mem device " + std::to_string(i) +
                 ", requested size: " + std::to_string(requested_size));
    }
  }

  if (num_allocated_replicas < NUM_REPLICAS) {
    CXL_FM_LOG("Failed to allocate enough regions for RPC connection, allocated: " +
               std::to_string(num_allocated_replicas) + ", required: " + std::to_string(NUM_REPLICAS));
    // Rollback any memory allocated to replicas that succeeded
    for (const AllocatedRegionInfo& allocated_region : allocated_regions) {
      allocated_region.backing_device->free(allocated_region.offset, allocated_region.size);
    }
    client_resp.status = CXL_IPC_STATUS_CHANNEL_ALLOC_FAILED;
    ::send(qemu_client_fd, &client_resp, sizeof(client_resp), 0);
    return;
  }

  // 4. Create RPC Connection struct
  // Monotonically increment channel_id
  // TODO: We would use UUID in the future
  channel_id_t assigned_channel_id = curr_channel_id++;
  if (curr_channel_id == UINT64_MAX) {
    curr_channel_id = 0;
  }

  RPCConnection rpc_connection;
  rpc_connection.channel_id = assigned_channel_id;
  rpc_connection.client_instance_id = client_id_str;
  rpc_connection.server_instance_id = chosen_server_info.server_instance_id;
  rpc_connection.service_name = service_name_str;
  rpc_connection.allocated_regions = allocated_regions;

  active_rpc_connections_[curr_channel_id] = std::move(rpc_connection);

  // 5. Send response to QEMU client and server
  client_resp.status = CXL_IPC_STATUS_OK;
  // TODO: Eventually will not be hardcoded
  // TODO: Currently, non-concurrent QEMU VM design, so all logical offsets are 0
  client_resp.channel_shm_size = requested_size;
  client_resp.channel_shm_offset = 0;

  // Prepare server payload
  cxl_ipc_rpc_new_client_notify_t server_notify_payload;
  server_notify_payload.type = CXL_MSG_TYPE_RPC_NEW_CLIENT_NOTIFY;
  server_notify_payload.channel_shm_size = requested_size;
  server_notify_payload.channel_shm_offset = 0;

  // TODO: Error handling if either send fails
  if (qemu_server_fd >= 0) {
    CXL_FM_LOG("Sending RPC_NEW_CLIENT_NOTIFY to server, fd: " + std::to_string(qemu_server_fd));
    ::send(qemu_server_fd, &server_notify_payload, sizeof(server_notify_payload), 0);
  } else {
    // This shud not have happened
    CXL_FM_LOG("Chosen server had invalid fd " + std::to_string(qemu_server_fd));
  }

  ::send(qemu_client_fd, &client_resp, sizeof(client_resp), 0);
  CXL_FM_LOG("Sent RPC_REQUEST_CHANNEL_RESP to client, fd: " + std::to_string(qemu_client_fd) +
               ", channel_id: " + std::to_string(assigned_channel_id) +
               ", size: " + std::to_string(client_resp.channel_shm_size) +
               ", offset: " + std::to_string(client_resp.channel_shm_offset));
}

void CXLFabricManager::handle_qemu_vm_message(int qemu_vm_fd) {
  // Peek to get message type
  uint8_t msg_type_header;
  ssize_t n = ::recv(qemu_vm_fd, &msg_type_header, sizeof(msg_type_header), MSG_PEEK | MSG_DONTWAIT);

  if (n <= 0) {  // This code block is unlikely
    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
      CXL_FM_LOG("Error peeking message type header: " + std::string(strerror(errno)));
    } else if (n == 0) {
      // Client disconnected
      CXL_FM_LOG("Client disconnected, fd: " + std::to_string(qemu_vm_fd));
      ::close(qemu_vm_fd);
      FD_CLR(qemu_vm_fd, &active_fds);
    }
    return;
  }

  CXL_FM_LOG("Received message type header: " + std::to_string(msg_type_header) + ", fd: " + std::to_string(qemu_vm_fd));

  switch(msg_type_header) {
  case CXL_MSG_TYPE_GET_MEM_SIZE_REQ:
    CXL_FM_LOG("Handling GET_MEM_SIZE_REQ");
    cxl_ipc_get_mem_size_req_t req;
    n = ::recv(qemu_vm_fd, &req, sizeof(req), MSG_WAITALL);
    if (n == sizeof(req)) {
      handle_get_mem_size(qemu_vm_fd);
    } else {
      CXL_FM_LOG("GET_MEM_SIZE_REQ recv error, expected " + std::to_string(sizeof(req)) + " bytes, got " + std::to_string(n));
    }
    break;
  case CXL_MSG_TYPE_WRITE_REQ:
    CXL_FM_LOG("Handling WRITE_REQ");
    cxl_ipc_write_req_t write_req;
    n = ::recv(qemu_vm_fd, &write_req, sizeof(write_req), MSG_WAITALL);
    if (n == sizeof(write_req)) {
      handle_write_mem_req(qemu_vm_fd, write_req);
    } else {
      CXL_FM_LOG("WRITE_REQ recv error, expected " + std::to_string(sizeof(write_req)) + " bytes, got " + std::to_string(n));
    }
    break;
  case CXL_MSG_TYPE_READ_REQ:
    CXL_FM_LOG("Handling READ_REQ");
    cxl_ipc_read_req_t read_req;
    n = ::recv(qemu_vm_fd, &read_req, sizeof(read_req), MSG_WAITALL);
    if (n == sizeof(read_req)) {
      handle_read_mem_req(qemu_vm_fd, read_req);
    } else {
      CXL_FM_LOG("READ_REQ recv error, expected " + std::to_string(sizeof(read_req)) + " bytes, got " + std::to_string(n));
    }
    break;
  case CXL_MSG_TYPE_RPC_REGISTER_SERVICE_REQ:
    CXL_FM_LOG("Handling RPC_REGISTER_SERVICE_REQ");
    cxl_ipc_rpc_register_service_req_t rpc_register_req;
    n = ::recv(qemu_vm_fd, &rpc_register_req, sizeof(rpc_register_req), MSG_WAITALL);
    if (n == sizeof(rpc_register_req)) {
      handle_register_rpc_service(qemu_vm_fd, rpc_register_req);
    } else {
      CXL_FM_LOG("RPC_REGISTER_SERVICE_REQ recv error, expected " + std::to_string(sizeof(rpc_register_req)) + " bytes, got " + std::to_string(n));
    }
    break;
  case CXL_MSG_TYPE_RPC_REQUEST_CHANNEL_REQ:
    CXL_FM_LOG("Handling RPC_REQUEST_CHANNEL_REQ");
    cxl_ipc_rpc_request_channel_req_t rpc_request_channel_req;
    n = ::recv(qemu_vm_fd, &rpc_request_channel_req, sizeof(rpc_request_channel_req), MSG_WAITALL);
    if (n == sizeof(rpc_request_channel_req)) {
      handle_rpc_request_channel_req(qemu_vm_fd, rpc_request_channel_req);
    } else {
      CXL_FM_LOG("RPC_REQUEST_CHANNEL_REQ recv error, expected " + std::to_string(sizeof(rpc_request_channel_req)) + " bytes, got " + std::to_string(n));
    }
    break;
  default:
    CXL_FM_LOG("Unknown message type header: " + std::to_string(msg_type_header) +
               ", fd: " + std::to_string(qemu_vm_fd));
    cxl_ipc_error_resp_t error_resp;
    error_resp.type = CXL_MSG_TYPE_ERROR_RESP;
    error_resp.status = CXL_IPC_STATUS_ERROR_INVALID_REQ;
    ::send(qemu_vm_fd, &error_resp, sizeof(error_resp), 0);
    break;
  }
}

//  --- Admin handlers ---
void CXLFabricManager::handle_admin_fail_memdev(int admin_client_fd, int memdev_index) {
  cxl_admin_fail_replica_resp_t resp;
  if (memdev_index < 0 || memdev_index >= mem_devices_.size()) {
    CXL_FM_LOG("Invalid memdev index: " + std::to_string(memdev_index) +
               ", valid range: [0, " + std::to_string(mem_devices_.size() - 1) + "]");
    resp.status = CXL_IPC_STATUS_ERROR_INVALID_REQ;
    ::send(admin_client_fd, &resp, sizeof(resp), 0);
    return;
  }
  // TODO: When we fail a memdevice, we would want to migrate all the state
  //       that it held to subsequent, healthy memdevs.
  //       For now, we will simply mark it as unhealthy which is the easiest.
  mem_devices_[memdev_index].mark_unhealthy();
  resp.status = CXL_IPC_STATUS_OK;
  ::send(admin_client_fd, &resp, sizeof(resp), 0);
}

void CXLFabricManager::handle_admin_command(int admin_client_fd) {
  uint8_t msg_type_header;
  ssize_t n;

  n = recv(admin_client_fd, &msg_type_header, sizeof(msg_type_header), MSG_PEEK | MSG_DONTWAIT);
  if (n <= 0) {
    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
      CXL_FM_LOG("Error receiving admin command: " + std::string(strerror(errno)));
    } else {
      CXL_FM_LOG("Admin client disconnected, fd: " + std::to_string(admin_client_fd));
    }
    return;
  }

  CXL_FM_LOG("Received admin command type header: " + std::to_string(msg_type_header) + ", fd: " + std::to_string(admin_client_fd));

  switch(msg_type_header) {
  case CXL_ADMIN_CMD_TYPE_FAIL_REPLICA:
    CXL_FM_LOG("Handling FAIL_REPLICA command");
    cxl_admin_fail_replica_req_t req;
    n = ::recv(admin_client_fd, &req, sizeof(req), MSG_WAITALL);
    if (n == sizeof(req)) {
      handle_admin_fail_memdev(admin_client_fd, req.memdev_index);
    } else {
      CXL_FM_LOG("FAIL_REPLICA command recv error, expected " + std::to_string(sizeof(req)) + " bytes, got " + std::to_string(n));
      return;
    }
    break;
  default:
    CXL_FM_LOG("Unknown admin command type: " + std::to_string(msg_type_header));
    return;
  }
}

//  --- Connection handlers ---

void CXLFabricManager::handle_new_qemu_vm_connection(int& max_fd) {
  struct sockaddr_un client_addr;
  socklen_t client_len = sizeof(client_addr);
  int client_fd = ::accept(main_listen_fd_, (struct sockaddr*)&client_addr, &client_len);

  if (client_fd < 0) {
    if (errno != EWOULDBLOCK && errno != EAGAIN) {
      // accept can fail with EWOULDBLOCK on a non-blocking read
      CXL_FM_LOG("Error accepting new QEMU VM connection: " + std::string(strerror(errno)));
    }
    return;
  }

  CXL_FM_LOG_P("Accepted new QEMU VM connection, fd: ", client_fd);
  FD_SET(client_fd, &active_fds);
  if (client_fd > max_fd) {
    max_fd = client_fd;
  }
}

void CXLFabricManager::handle_new_admin_connection() {
  struct sockaddr_un admin_addr;
  socklen_t admin_len = sizeof(admin_addr);
  int admin_client_fd = ::accept(admin_listen_fd_, (struct sockaddr*)&admin_addr, &admin_len);

  if (admin_client_fd < 0) {
    if (errno != EWOULDBLOCK && errno != EAGAIN) {
      CXL_FM_LOG("Error accepting new admin connection: " + std::string(strerror(errno)));
    }
    return;
  }
  // Admin commands are one shot
  CXL_FM_LOG_P("Accepted new admin connection, fd: ", admin_client_fd);
  handle_admin_command(admin_client_fd);
  ::close(admin_client_fd);
  CXL_FM_LOG_P("Closed admin connection, fd: ", admin_client_fd);
}

void CXLFabricManager::run() {
  CXL_FM_LOG("Starting CXL Fabric Manager event loop.");
  
  fd_set read_fds;
  int max_fd = std::max(main_listen_fd_, admin_listen_fd_);

  FD_ZERO(&active_fds);
  FD_SET(main_listen_fd_, &active_fds);
  FD_SET(admin_listen_fd_, &active_fds);

  std::vector<int> active_qemu_client_fds;

  while (true) {
    read_fds = active_fds;
    
    int activity = ::select(max_fd + 1, &read_fds, nullptr, nullptr, nullptr);

    if (activity < 0 && errno != EINTR) {
      CXL_FM_LOG("Error in select: " + std::string(strerror(errno)));
      break; // Exit on select error
    }
    // This shouldn't happen because we are not timing out here
    if (activity == 0) {
      CXL_FM_LOG("No activity, continuing to wait...");
      continue; // No activity, continue waiting
    }

    if (FD_ISSET(main_listen_fd_, &read_fds)) {
      handle_new_qemu_vm_connection(max_fd);
    }

    if (FD_ISSET(admin_listen_fd_, &read_fds)) {
      handle_new_admin_connection();
    }

    // Check existing QEMU clients for data
    for (int qemu_client_fd = 0; qemu_client_fd <= max_fd; qemu_client_fd++) {
      if (qemu_client_fd != main_listen_fd_ && qemu_client_fd != admin_listen_fd_ && FD_ISSET(qemu_client_fd, &read_fds)) {
        // We do a peek to check if this is a disconnect
        char peek_buf;
        ssize_t peek_ret = recv(qemu_client_fd, &peek_buf, 1, MSG_PEEK | MSG_DONTWAIT);

        if (peek_ret > 0) { // Data available
          handle_qemu_vm_message(qemu_client_fd);
        } else if (peek_ret == 0) { // Client disconnect
          CXL_FM_LOG("Client disconnected, fd: " + std::to_string(qemu_client_fd));
          close(qemu_client_fd);
          FD_CLR(qemu_client_fd, &active_fds);
          // Recalculate max fd
          if (qemu_client_fd == max_fd) {
            while (max_fd > main_listen_fd_ && FD_ISSET(max_fd, &active_fds)) {
              max_fd--;
            }
          }
        } else { // Error
          if (errno != EAGAIN && errno != EWOULDBLOCK) {  
            // EAGAIN and EWOULDBLOCK are ok
            CXL_FM_LOG("Error reading from QEMU client fd " + std::to_string(qemu_client_fd) + ": " + std::string(strerror(errno)));
            close(qemu_client_fd);
            FD_CLR(qemu_client_fd, &active_fds);
            // Recalculate max fd
            if (qemu_client_fd == max_fd) {
              while (max_fd > main_listen_fd_ && !FD_ISSET(max_fd, &active_fds)) {
                max_fd--;
              }
            }
          }
        }

      }
    }
  }

  CXL_FM_LOG("CXL Fabric Manager event loop terminated.");
}


// --- CXL Fabric Manager Constructors and resource management ---
CXLFabricManager::CXLFabricManager(Config config) : config_(std::move(config)) {
  CXL_FM_LOG("CXL Fabric Manager created.");
  if (config_.replica_paths.empty()) {
    CXL_FM_LOG("No replica paths provided in configuration.");
    throw std::invalid_argument("Replica paths cannot be empty.");
  }
  // Try to init all the mem devices specified in the config
  for (const auto& path : config_.replica_paths) {
    try {
      // Note to self: destructor is called here at end of scope
      CXL_FM_LOG("Oooga");
      CXLMemDevice memdevice(path, config_.replica_mem_size);
      mem_devices_.push_back(std::move(memdevice));
      CXL_FM_LOG("Booga Oooga");
    } catch (const std::exception& e) {
      CXL_FM_LOG_P("Failed to create memory device for path: ", path);
      CXL_FM_LOG_P("Error: ", e.what());
      // No need to throw an error here, we can just continue without
      // Since the end goal is to tolerate real device failure
    }
  }

  // However, we should throw an error here if we were unable to init a single
  // memory device at all.
  if (mem_devices_.empty()) {
    throw std::runtime_error("No valid memory devices initialized.");
  }

  // Init the sockets, fail if error
  if (!setup_socket(main_listen_fd_, config_.main_socket_path)) {
    CXL_FM_LOG("Failed to set up main socket at " + config_.main_socket_path);
    throw std::runtime_error("Failed to set up main socket.");
  }

  if (!setup_socket(admin_listen_fd_, config_.admin_socket_path)) {
    CXL_FM_LOG("Failed to set up admin socket at " + config_.admin_socket_path);
    throw std::runtime_error("Failed to set up admin socket.");
  }

  CXL_FM_LOG("CXL Fabric Manager initialized with main socket: " + config_.main_socket_path +
             " and admin socket: " + config_.admin_socket_path);
}

CXLFabricManager::~CXLFabricManager() {
  if (main_listen_fd_ >= 0) {
    close(main_listen_fd_);
    main_listen_fd_ = -1;
    ::unlink(config_.main_socket_path.c_str());
  }
  if (admin_listen_fd_ >= 0) {
    close(admin_listen_fd_);
    admin_listen_fd_ = -1;
    ::unlink(config_.admin_socket_path.c_str());
  }
  CXL_FM_LOG("CXL Fabric Manager destroyed.");
}

bool CXLFabricManager::setup_socket(int& socket_fd, const std::string& socket_path) {
  socket_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (socket_fd < 0) {
    CXL_FM_LOG("Failed to create socket for " + socket_path + ": " + strerror(errno));
    return false;
  } 
  // Retrieve flags, then add non-blocking flag
  int flags = fcntl(socket_fd, F_GETFL, 0);
  if (flags == -1) {
    CXL_FM_LOG("Failed to get socket flags for " + socket_path + ": " + strerror(errno));
    goto close_fd;
  }
  if (fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    CXL_FM_LOG("Failed to set socket to non-blocking for " + socket_path + ": " + strerror(errno));
    goto close_fd;
  }
  // Standard UNIX socket setup
  struct sockaddr_un addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  std::strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);
  ::unlink(socket_path.c_str()); // Remove any existing socket file

  if (::bind(socket_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    CXL_FM_LOG("Failed to bind socket for " + socket_path + ": " + strerror(errno));
    goto close_fd;
  }

  if (::listen(socket_fd, SOMAXCONN) < 0) {
    CXL_FM_LOG("Failed to listen on socket for " + socket_path + ": " + strerror(errno));
    goto close_fd;
  }
  CXL_FM_LOG("Socket setup successful for " + socket_path + ", fd: " + std::to_string(socket_fd));
  return true;

close_fd:
  ::close(socket_fd);
  socket_fd = -1;
  return false;
}
}

int main(int argc, char *argv[]) {
  if (argc < (1 + 3 + NUM_REPLICAS)) {
    std::cerr << "Usage: " << argv[0] << " <main_socket_path> <admin_socket_path> <replica_size_MiB> [replica_paths...]" << std::endl;
    return EXIT_FAILURE;
  }

  cxl_fm::CXLFabricManager::Config config;
  config.main_socket_path = argv[1];
  config.admin_socket_path = argv[2];
  try {
    config.replica_mem_size = static_cast<uint64_t>(std::stoull(argv[3])) * 1024 * 1024;
  } catch (const std::exception& e) {
    std::cerr << "Invalid replica size: " << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  for (int i = 0; i < NUM_REPLICAS; i++) {
    config.replica_paths.push_back(argv[4+i]);
  }

  cxl_fm::CXLFabricManager fm(config);
  CXL_FM_LOG("CXL Fabric Manager initialized with config: " + config.main_socket_path + ", " +
             config.admin_socket_path + ", " + std::to_string(config.replica_mem_size) + " bytes");
  fm.run();
  CXL_FM_LOG("CXL Fabric Manager run completed.");
  return EXIT_SUCCESS;
}