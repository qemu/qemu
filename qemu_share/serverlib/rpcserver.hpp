#ifndef DIANCIE_RPC_LIB_HPP
#define DIANCIE_RPC_LIB_HPP

#include "../includes/cxl_switch_ipc.h"
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include "../includes/ioctl_defs.h"

namespace diancie {

// Represents an active, mmapped connection between the
// server instance and a client instance.
// Maintains the server's queue entry and the client's queue entry
// which are fixed size offsets
// Both queue entries are 64 bits, 1 bit for poll and 63 bits for indexing
// into the shared memory region heap

// Server Queue | Client Queue | Shared Memory Heap
struct QueueEntry {
  uint64_t queue_entry;
};

struct Connection {
public:
  Connection(uint64_t mapped_base, uint32_t size, uint64_t channel_id);
  uint64_t mapped_base_;
  uint32_t mapped_size_;
  uint64_t channel_id_;
};

using ClientId = std::string;

class DiancieServer {
public:
  // Constructors
  DiancieServer() = delete;
  
  DiancieServer(const std::string& device_path, const std::string& service_name, const std::string& instance_id);
  
  ~DiancieServer();

  DiancieServer(const DiancieServer&) = delete;
  DiancieServer& operator=(const DiancieServer&) = delete;

  DiancieServer(DiancieServer&&) = delete;
  DiancieServer& operator=(DiancieServer&&) = delete;

  void register_rpc_function();
  void service_client(Connection connection);
  void run_server_loop();

private:
  std::string device_path_;
  int device_fd_ = -1;

  // For logging purposes
  std::string service_name_;
  std::string instance_id_;
  
  void *bar0_base_ = nullptr;
  size_t bar0_size_ = 0;

  void *bar1_base_ = nullptr;
  size_t bar1_size_ = 0;
  
  // TODO: now i gotta map this
  void *bar2_base_ = nullptr;
  size_t bar2_size_ = 0;

  int eventfd_notify_ = -1;
  int eventfd_cmd_ready_ = -1;

  // Poll method to wait for incoming client connections
  Connection wait_for_new_client_notification(int timeout_ms);
  uint32_t get_command_status();

  std::vector<std::thread> clients_;

  bool setup_eventfd(int& efd, unsigned int ioctl_cmd);
  void cleanup_eventfd(int& efd);

  static constexpr off_t BAR0_MMAP_OFFSET = 0 * 4096; // MMAP_OFFSET_PGOFF_BAR0
  static constexpr off_t BAR1_MMAP_OFFSET = 1 * 4096; // MMAP_OFFSET_PGOFF_BAR1
  static constexpr off_t BAR2_MMAP_OFFSET = 2 * 4096; // MMAP_OFFSET_PGOFF_BAR2
  static constexpr size_t DEFAULT_BAR0_SIZE = 4096;
  static constexpr size_t DEFAULT_BAR1_SIZE = 4096;
  static constexpr size_t DEFAULT_BAR2_SIZE = 256 * 1024 * 1024;

  bool wait_for_command_response(int timeout_ms);
  uint32_t get_notification_status();
  void clear_notification_status(uint32_t bits_to_clear);
  bool send_command(const void* req, size_t size);
  // Called once in constructor
  bool register_service();
  // Called in destructor?
  void deregister_service();

};

}


#endif // DIANCIE_RPC_LIB_HPP