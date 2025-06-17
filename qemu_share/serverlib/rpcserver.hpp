#ifndef DIANCIE_RPC_LIB_HPP
#define DIANCIE_RPC_LIB_HPP

#include "../includes/cxl_switch_ipc.h"
#include <cstdint>
#include <memory>
#include <string>
#include "../includes/ioctl_defs.h"

namespace diancie {

// Represents an active, mmapped connection between the
// server instance and a client instance.
class Connection {
public:
  Connection(int fd, uint64_t size);
  ~Connection();

  Connection(const Connection&) = delete;
  Connection& operator=(const Connection&) = delete;

  void* get_base_address() const {
    return mapped_base_;
  }

  uint64_t get_size() const {
    return mapped_size_;
  }
private:
  int fd_;
  void* mapped_base_ = nullptr;
  uint64_t mapped_size_ = 0;
};


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

  // Poll method to wait for incoming client connections
  cxl_ipc_rpc_new_client_notify_t wait_for_new_client_notification(int timeout_ms);
  // Accept a notification if possible and return a handle to the mapped memory channel
  std::unique_ptr<Connection> accept_connection(const cxl_ipc_rpc_new_client_notify_t& notif);
  uint32_t get_command_status();

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

  void *bar2_base_ = nullptr;
  size_t bar2_size_ = 0;

  int eventfd_notify_ = -1;
  int eventfd_cmd_ready_ = -1;

  bool setup_eventfd(int& efd, unsigned int ioctl_cmd);
  void cleanup_eventfd(int& efd);

  static constexpr off_t BAR0_MMAP_OFFSET = 0 * 4096; // MMAP_OFFSET_PGOFF_BAR0
  static constexpr off_t BAR1_MMAP_OFFSET = 1 * 4096; // MMAP_OFFSET_PGOFF_BAR1
  static constexpr size_t DEFAULT_BAR0_SIZE = 4096;
  static constexpr size_t DEFAULT_BAR1_SIZE = 4096;

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