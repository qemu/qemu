#ifndef QEMU_CXL_CONNECTOR_HPP
#define QEMU_CXL_CONNECTOR_HPP

#include "a_cxl_connector.hpp"
#include <string>
#include <iostream>

namespace diancie {

/// Represents a connection in QEMU space.
/// TODO: Might have to be ported around
struct QEMUConnection : AbstractCXLConnection {
public:
  QEMUConnection(uint64_t mapped_base, uint32_t size, uint64_t channel_id)
    : mapped_base_(mapped_base), mapped_size_(size), channel_id_(channel_id) {
    std::cout << "QEMUConnection created with base: " << mapped_base_ 
              << ", size: " << size 
              << ", channel_id: " << channel_id << std::endl;
  }
  // Simple accessors - i am still unsure over interface details for these
  uint64_t mapped_base_;
  uint32_t mapped_size_;
  uint64_t channel_id_;

  uint64_t get_base() override { return mapped_base_; }
  uint64_t get_size() override { return mapped_size_; }
  uint64_t get_channel_id() override { return channel_id_; }
};

class QEMUCXLConnector : protected AbstractCXLConnector {
public:
  QEMUCXLConnector(const std::string &device_path);
  ~QEMUCXLConnector();

protected:

  void *bar0_base_ = nullptr;
  size_t bar0_size_ = 0;

  void *bar1_base_ = nullptr;
  size_t bar1_size_ = 0;
  
  void *bar2_base_ = nullptr;
  size_t bar2_size_ = 0;

private:
  std::string device_path_;

  int device_fd_ = -1;
  
  bool setup_eventfd(int& efd, unsigned int ioctl_cmd);
  void cleanup_eventfd(int& efd);

  static constexpr off_t BAR0_MMAP_OFFSET = 0 * 4096; // MMAP_OFFSET_PGOFF_BAR0
  static constexpr off_t BAR1_MMAP_OFFSET = 1 * 4096; // MMAP_OFFSET_PGOFF_BAR1
  static constexpr off_t BAR2_MMAP_OFFSET = 2 * 4096; // MMAP_OFFSET_PGOFF_BAR2
  static constexpr size_t DEFAULT_BAR0_SIZE = 4096;
  static constexpr size_t DEFAULT_BAR1_SIZE = 4096;
  static constexpr size_t DEFAULT_BAR2_SIZE = 1024 * 1024 * 1024;

protected:
  // TODO: Make these private
  int eventfd_notify_ = -1;
  int eventfd_cmd_ready_ = -1;
  QEMUConnection wait_for_new_client_notification(int timeout_ms);

  std::optional<CXLEventData> wait_for_event(int timeout_ms) override;
  std::optional<CXLEventData> check_for_new_client();
  std::optional<CXLEventData> check_for_closed_channel();
  std::optional<CXLEventData> check_for_disconnect();

  bool send_command(const void* req, size_t size) override;
  bool recv_response(void* req, size_t size) override;
  bool wait_for_command_response(int timeout_ms) override;
  uint32_t get_command_status();
  uint32_t get_notification_status() override;
  void clear_notification_status(uint32_t bits_to_clear);
  // In the impl now, both server/client must configure their QEMU device
  bool set_memory_window(uint64_t offset, uint64_t size, uint64_t channel_id) override;
  

  void write_u64(uint64_t offset, uint64_t value);
  uint64_t read_u64(uint64_t offset);

};

}

#endif