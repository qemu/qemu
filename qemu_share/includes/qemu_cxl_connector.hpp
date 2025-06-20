#ifndef QEMU_CXL_CONNECTOR_HPP
#define QEMU_CXL_CONNECTOR_HPP

#include "a_cxl_connector.hpp"
#include <string>

namespace diancie {
class QEMUCXLConnector : protected AbstractCXLConnector {
public:
  QEMUCXLConnector(const std::string &device_path);
  ~QEMUCXLConnector();

  void write_u64(uint64_t offset, uint64_t value);
  uint64_t read_u64(uint64_t offset);
private:
  std::string device_path_;

  int device_fd_ = -1;
  
  void *bar0_base_ = nullptr;
  size_t bar0_size_ = 0;

  void *bar1_base_ = nullptr;
  size_t bar1_size_ = 0;
  
  // TODO: now i gotta map this
  void *bar2_base_ = nullptr;
  size_t bar2_size_ = 0;

  bool setup_eventfd(int& efd, unsigned int ioctl_cmd);
  void cleanup_eventfd(int& efd);

  static constexpr off_t BAR0_MMAP_OFFSET = 0 * 4096; // MMAP_OFFSET_PGOFF_BAR0
  static constexpr off_t BAR1_MMAP_OFFSET = 1 * 4096; // MMAP_OFFSET_PGOFF_BAR1
  static constexpr off_t BAR2_MMAP_OFFSET = 2 * 4096; // MMAP_OFFSET_PGOFF_BAR2
  static constexpr size_t DEFAULT_BAR0_SIZE = 4096;
  static constexpr size_t DEFAULT_BAR1_SIZE = 4096;
  static constexpr size_t DEFAULT_BAR2_SIZE = 256 * 1024 * 1024;

protected:
  // TODO: Make these private
  int eventfd_notify_ = -1;
  int eventfd_cmd_ready_ = -1;

  bool send_command(const void* req, size_t size);
  bool recv_response(void* req, size_t size);
  bool wait_for_command_response(int timeout_ms);
  uint32_t get_command_status();
  uint32_t get_notification_status();
  void clear_notification_status(uint32_t bits_to_clear);
  // In the impl now, both server/client must configure their QEMU device
  bool set_memory_window(uint64_t offset, uint64_t size);


};

}

#endif