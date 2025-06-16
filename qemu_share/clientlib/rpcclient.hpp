#ifndef DIANCIE_RPC_CLIENT_HPP
#define DIANCIE_RPC_CLIENT_HPP

#include <cstdint>
#include <optional>
#include <string>

#include "../includes/cxl_switch_ipc.h"
#include "../includes/ioctl_defs.h"

namespace diancie {

// The memory region that the client library is allocated
struct ChannelInfo {
  uint64_t offset;
  uint64_t size;
};

// The client library manages the low-level interactions for the user program.
// 1. Discover service and get allocated channel
class DiancieClient {
public:
  DiancieClient() = delete;
  DiancieClient(const std::string& device_path);
  ~DiancieClient();
  // Find a service via the FM and request a comms channel
  std::optional<ChannelInfo> request_channel(const std::string& service_name, const std::string& instance_id);

  // Configure local QEMU device's BAR2 to map to channel
  bool set_memory_window(uint64_t offset, uint64_t size);
  // Generic interface methods
  void write_u64(uint64_t offset, uint64_t value);
  uint64_t read_u64(uint64_t offset);
  
private:
  std::string device_path_;
  int device_fd_ = -1;
  
  void* bar0_base_ = nullptr;  // mailbox
  size_t bar0_size_ = 0;
  
  void* bar1_base_ = nullptr;  // control registers
  size_t bar1_size_ = 0;
  
  void* bar2_base_ = nullptr;  // data window
  size_t bar2_size_ = 0;

  int eventfd_cmd_ready_ = -1;

  bool setup_eventfd(int& efd, unsigned int ioctl_cmd);
  bool wait_for_command_response(int timeout_ms);
  uint32_t get_command_status();

  static constexpr off_t BAR0_MMAP_OFFSET = 0 * 4096; // MMAP_OFFSET_PGOFF_BAR0
  static constexpr off_t BAR1_MMAP_OFFSET = 1 * 4096; // MMAP_OFFSET_PGOFF_BAR1
  static constexpr off_t BAR2_MMAP_OFFSET = 2 * 4096; // MMAP_OFFSET_PGOFF_BAR1
  static constexpr size_t DEFAULT_BAR_SIZE = 4096;
  static constexpr size_t DEFAULT_BAR2_SIZE = 256 * 1024 * 1024;
};

}

#endif