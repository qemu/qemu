#ifndef CXL_FM_HPP
#define CXL_FM_HPP

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <sys/select.h>

#include "memdevice.hpp"
#include "../includes/cxl_switch_ipc.h"

namespace cxl_fm {

#define NUM_REPLICAS 3

using service_name_t = std::string;
using channel_id_t = uint64_t;
using instance_id_t = std::string;

/**
  Information about a registered RPC Server instance.
*/
struct RPCServerInstanceInfo {
  std::string server_instance_id;
  // The QEMU VM's fd that can export this service
  int qemu_client_fd;
  
  RPCServerInstanceInfo(std::string instance_id, int client_fd)
      : server_instance_id(std::move(instance_id)), qemu_client_fd(client_fd) {}
};

/**
  Information about an allocated region of memory for an RPC connection for one
  CXL mem device.
*/
struct AllocatedRegionInfo {
  CXLMemDevice* backing_device;
  uint64_t offset;
  uint32_t size;
};

/**
  Information about an RPC connection.
*/
struct RPCConnection {
  channel_id_t channel_id;
  std::string client_instance_id;
  int client_fd;
  std::string server_instance_id;
  int server_fd;
  service_name_t service_name;
  // Memory regions that back up this connection
  std::vector<AllocatedRegionInfo> allocated_regions;
};

class CXLFabricManager {

public:
  struct Config {
    std::string main_socket_path = CXL_SWITCH_SERVER_SOCKET_PATH_DEFAULT;
    std::string admin_socket_path = CXL_SWITCH_SERVER_ADMIN_SOCKET_PATH_DEFAULT;
    std::vector<std::string> replica_paths;
    // TODO: Change this to allow replicas of different size
    uint64_t replica_mem_size;
  };

  // Standard deletion for copy/move
  CXLFabricManager(Config config);
  ~CXLFabricManager();

  CXLFabricManager(const CXLFabricManager &) = delete;
  CXLFabricManager &operator=(const CXLFabricManager &) = delete;

  CXLFabricManager(CXLFabricManager &&) = delete;
  CXLFabricManager &operator=(CXLFabricManager &&) = delete;

  void run();
  void shutdown();

private:
  Config config_;
  std::vector<CXLMemDevice> mem_devices_;
  std::map<service_name_t, std::vector<RPCServerInstanceInfo>> service_registry_;
  std::map<channel_id_t, RPCConnection> active_rpc_connections_;
  // Track which channels a QEMU VM (fd) is involved in
  std::map<int, std::vector<channel_id_t>> fd_to_channel_ids_;

  int main_listen_fd_ = -1;
  int admin_listen_fd_ = -1;
  std::mutex state_mutex_;
  channel_id_t curr_channel_id = 0;

  bool setup_socket(int& socket_fd, const std::string& socket_path);

  // --- Event loop management ---
  
  // The current design uses select() to handle event management
  fd_set active_fds_;

  // Handler functions for main requests
  void handle_get_mem_size(int qemu_vm_fd);
  void handle_write_mem_req(int qemu_vm_fd, const cxl_ipc_write_req_t& req);
  void handle_read_mem_req(int qemu_vm_fd, const cxl_ipc_read_req_t& req);
  void handle_register_rpc_service(int qemu_vm_fd, const cxl_ipc_rpc_register_service_req_t& req);
  void handle_deregister_rpc_service(int qemu_vm_fd, const cxl_ipc_rpc_deregister_service_req_t& req);
  void handle_rpc_request_channel_req(int qemu_client_fd, const cxl_ipc_rpc_request_channel_req_t& req);
  void handle_rpc_release_channel_req(int qemu_client_fd, const cxl_ipc_rpc_release_channel_req_t& req);

  // Handler functions for admin requests
  void handle_admin_fail_memdev(int admin_client_fd, int memdev_index);

  // Handler functions for socket events
  void handle_new_qemu_vm_connection(int& max_fd);
  void handle_new_admin_connection();
  void handle_qemu_vm_message(int qemu_vm_fd);
  void handle_admin_command(int admin_client_fd);
  void handle_qemu_disconnect(int qemu_vm_fd, int& max_fd);

  // Helper functions for cleaning up
  void cleanup_channels_by_id(const std::vector<channel_id_t>& channels_to_clean);
  void cleanup_services_by_fd(int fd);
};

}

#endif