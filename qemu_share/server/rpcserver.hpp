#ifndef DIANCIE_RPC_LIB_HPP
#define DIANCIE_RPC_LIB_HPP

#include "../includes/cxl_switch_ipc.h"
#include <string>
#include <asm-generic/ioctl.h>

namespace diancie {

#include "../includes/ioctl_defs.h"

#define REG_COMMAND_DOORBELL 0x00
#define REG_COMMAND_STATUS   0x04
#define REG_NOTIF_STATUS     0x08

#define CMD_STATUS_IDLE                    0x00
#define CMD_STATUS_PROCESSING              0x01
#define CMD_STATUS_RESPONSE_READY          0x02
#define CMD_STATUS_ERROR_IPC               0xE0

#define NOTIF_STATUS_NONE               0x00
#define NOTIF_STATUS_NEW_CLIENT         0x01



class DiancieServer {
public:
  // Constructors
  DiancieServer() = delete;
  
  DiancieServer(const std::string& device_path);
  
  ~DiancieServer();

  DiancieServer(const DiancieServer&) = delete;
  DiancieServer& operator=(const DiancieServer&) = delete;

  DiancieServer(DiancieServer&&) = delete;
  DiancieServer& operator=(DiancieServer&&) = delete;

  // ---
  bool register_service(const std::string& service_name, const std::string& instance_id);
  cxl_ipc_rpc_new_client_notify_t wait_for_new_client_notification(int timeout_ms);
  uint32_t get_command_status();


private:
  std::string device_path_;
  int device_fd_ = -1;
  
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

};

}


#endif // DIANCIE_RPC_LIB_HPP