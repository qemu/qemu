#include "a_cxl_connector.hpp"
#include "qemu_cxl_connector.hpp"
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <sys/poll.h>
#include <unistd.h>
#include "sys/eventfd.h"
#include "sys/mman.h"
#include "cxl_switch_ipc.h"
#include "ioctl_defs.h"
#include "sys/ioctl.h"

namespace diancie {



QEMUCXLConnector::QEMUCXLConnector(const std::string &device_path)
  : device_path_(device_path)
{
  // 1. Open device fd
  device_fd_ = open(device_path_.c_str(), O_RDWR);
  if (device_fd_ < 0) {
    throw std::runtime_error("Failed to open device: " + device_path_);
  }
  // 2. mmap bars
  bar0_size_ = DEFAULT_BAR0_SIZE;
  bar0_base_ = mmap(nullptr, bar0_size_, PROT_READ | PROT_WRITE, MAP_SHARED,
                    device_fd_, BAR0_MMAP_OFFSET);
  if (bar0_base_ == MAP_FAILED) {
    close(device_fd_);
    throw std::runtime_error("Failed to mmap BAR0: " + device_path_);
  }

  bar1_size_ = DEFAULT_BAR1_SIZE;
  bar1_base_ = mmap(nullptr, bar1_size_, PROT_READ | PROT_WRITE, MAP_SHARED,
                    device_fd_, BAR1_MMAP_OFFSET);
  if (bar1_base_ == MAP_FAILED) {
    munmap(bar0_base_, bar0_size_);
    close(device_fd_);
    throw std::runtime_error("Failed to mmap BAR1: " + device_path_);
  }

  // TODO: Temporary workaround until I figure out how to expose CXL mem frfr
  bar2_size_ = DEFAULT_BAR2_SIZE;
  bar2_base_ = mmap(nullptr, bar2_size_, PROT_READ | PROT_WRITE, MAP_SHARED,
                    device_fd_, BAR2_MMAP_OFFSET);
  if (bar2_base_ == MAP_FAILED) {
    munmap(bar1_base_, bar1_size_);
    munmap(bar0_base_, bar0_size_);
    close(device_fd_);
    throw std::runtime_error("Failed to mmap BAR2: " + device_path_);
  }

  // 3. Setup event fds
  if (!setup_eventfd(eventfd_notify_, CXL_SWITCH_IOCTL_SET_EVENTFD_NOTIFY)) {
    munmap(bar2_base_, bar2_size_);
    munmap(bar1_base_, bar1_size_);
    munmap(bar0_base_, bar0_size_);
    close(device_fd_);
    throw std::runtime_error("Failed to setup eventfd for notifications");
  }

  if (!setup_eventfd(eventfd_cmd_ready_, CXL_SWITCH_IOCTL_SET_EVENTFD_CMD_READY)) {
    cleanup_eventfd(eventfd_notify_);
    munmap(bar2_base_, bar2_size_);
    munmap(bar1_base_, bar1_size_);
    munmap(bar0_base_, bar0_size_);
    close(device_fd_);
    throw std::runtime_error("Failed to setup eventfd for command ready");
  }

  std::cout << "QEMU CXL Connector initialized. Device = " << device_path_
          << ", BAR0 mmapped at " << bar0_base_ << ", BAR1 mmapped at "
          << bar1_base_ << ", BAR2 mapped at " << bar2_base_ << ", notify_efd " << eventfd_notify_
          << ", cmd_ready_efd " << eventfd_cmd_ready_ << std::endl;
}

QEMUCXLConnector::~QEMUCXLConnector() {
  cleanup_eventfd(eventfd_cmd_ready_);
  cleanup_eventfd(eventfd_notify_);
  munmap(bar2_base_, bar2_size_);
  munmap(bar1_base_, bar1_size_);
  munmap(bar0_base_, bar0_size_);
  close(device_fd_);
  std::cout << "QEMU CXL Connector resources cleaned up." << std::endl;
}

bool QEMUCXLConnector::setup_eventfd(int& efd, unsigned int ioctl_cmd) {
  efd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
  if (efd < 0) return false;
  if (ioctl(device_fd_, ioctl_cmd, &efd) < 0) {
    close(efd);
    efd = -1;
    return false;
  }
  return true;
}

void QEMUCXLConnector::cleanup_eventfd(int& efd_member) {
  if (efd_member >= 0) {
    close(efd_member);
    efd_member = -1;
  }
}

bool QEMUCXLConnector::wait_for_command_response(int timeout_ms) {
  struct pollfd pfd;
  pfd.fd = eventfd_cmd_ready_;
  pfd.events = POLLIN;

  std::cout << "QEMUCXLConnector: Polling on command_ready_efd " << eventfd_cmd_ready_ << std::endl;
  int ret = poll(&pfd, 1, timeout_ms);

  if (ret < 0) {
    std::cerr << "QEMUCXLConnector: Poll error: " << strerror(errno) << std::endl;
    return false;
  } else if (ret == 0) {
    std::cerr << "QEMUCXLConnector: Poll timeout after " << timeout_ms << " ms." << std::endl;
    return false;
  } else if (pfd.revents & POLLIN) {
    uint64_t event_count;
    ssize_t read_bytes = read(eventfd_cmd_ready_, &event_count, sizeof(event_count));
    if (read_bytes != sizeof(event_count)) {
      std::cerr << "QEMUCXLConnector: Failed to read from eventfd: " << strerror(errno) << std::endl;
      return false;
    }
    std::cout << "QEMUCXLConnector: Eventfd read successfully, value = " << event_count << std::endl;
    return true;
  }

  std::cerr << "QEMUCXLConnector: Unexpected poll revents: " << pfd.revents << std::endl;
  return false;
}

bool QEMUCXLConnector::send_command(const void* req, size_t size) {
  memcpy(bar0_base_, req, size);
  volatile uint32_t* doorbell = static_cast<volatile uint32_t*>(static_cast<void*>(static_cast<char*>(bar1_base_) + REG_COMMAND_DOORBELL));
  *doorbell = 1;
  if (!wait_for_command_response(5000)) {
    std::cerr << "Timeout waiting for command response." << std::endl;
    return false;
  }

  return get_command_status() == CMD_STATUS_RESPONSE_READY;
}

bool QEMUCXLConnector::recv_response(void* resp, size_t size) {
  memcpy(resp, bar0_base_, size);
  return true;
}

uint32_t QEMUCXLConnector::get_command_status() {
  volatile uint32_t* status_reg_ptr = static_cast<volatile uint32_t*>(
    static_cast<void*>(static_cast<char*>(bar1_base_) + REG_COMMAND_STATUS)
  );
  return *status_reg_ptr;
}

uint32_t QEMUCXLConnector::get_notification_status() {
  volatile uint32_t* status_reg_ptr = static_cast<volatile uint32_t*>(
    static_cast<void*>(static_cast<char*>(bar1_base_) + REG_NOTIF_STATUS)
  );
  return *status_reg_ptr;
}

// This method clears the notif and also clears the interrupt status
// as it writes to the bar1 register
void QEMUCXLConnector::clear_notification_status(uint32_t bits_to_clear) {
  volatile uint32_t* status_reg_ptr = static_cast<volatile uint32_t*>(
    static_cast<void*>(static_cast<char*>(bar1_base_) + REG_NOTIF_STATUS)
  );
  *status_reg_ptr = bits_to_clear;
  std::cout << "DiancieServer: Cleared notification status bits: 0x" << std::hex << bits_to_clear << std::dec << std::endl;
}

bool QEMUCXLConnector::set_memory_window(uint64_t offset, uint64_t size, uint64_t channel_id) {
  cxl_ipc_rpc_set_bar2_window_req_t req;
  std::memset(&req, 0, sizeof(req));
  req.type = CXL_MSG_TYPE_RPC_SET_BAR2_WINDOW_REQ;
  req.offset = offset;
  req.size = size;
  req.channel_id = channel_id;

  std::cout << "QEMUCXLConnector: Sending memory window request "
            << "Offset: 0x" << std::hex << req.offset
            << ", Size: 0x" << req.size << std::dec 
            << ", Channel ID: " << channel_id << std::endl;

  if (send_command(&req, sizeof(req))) {
    cxl_ipc_rpc_set_bar2_window_resp_t resp;
    recv_response(&resp, sizeof(resp));
    return resp.status == CXL_IPC_STATUS_OK;
  }

  return false;
}

void QEMUCXLConnector::write_u64(uint64_t offset, uint64_t value) {
  if (bar2_base_ == nullptr) {
    throw std::runtime_error("BAR2 memory window not set. Call set_memory_window() first.");
  }
  if (offset + sizeof(value) > bar2_size_) {
    throw std::out_of_range("Write out of bounds of BAR2 memory window.");
  }
  volatile uint64_t* addr = static_cast<volatile uint64_t*>(static_cast<void*>(static_cast<char*>(bar2_base_) + offset));
  *addr = value;
}

uint64_t QEMUCXLConnector::read_u64(uint64_t offset) {
  if (bar2_base_ == nullptr) {
    throw std::runtime_error("BAR2 memory window not set. Call set_memory_window() first.");
  }
  if (offset + sizeof(uint64_t) > bar2_size_) {
    throw std::out_of_range("Read out of bounds of BAR2 memory window.");
  }
  volatile uint64_t* addr = static_cast<volatile uint64_t*>(static_cast<void*>(static_cast<char*>(bar2_base_) + offset));
  return *addr;
}

std::optional<CXLEventData> QEMUCXLConnector::wait_for_event(int timeout_ms) {
  struct pollfd pfds;

  pfds.fd = eventfd_notify_;
  pfds.events = POLLIN;

  // std::cout << "QEMUCXLConnector: Polling on notify_efd " << eventfd_notify_ << std::endl;

  int ret = poll(&pfds, 1, timeout_ms);

  if (ret < 0) {
    std::cerr << "QEMUCXLConnector: Poll error: " << strerror(errno) << std::endl;
    return std::nullopt;
  } else if (ret == 0) {
    // std::cerr << "QEMUCXLConnector: Poll timeout after " << timeout_ms << " ms." << std::endl;
    return std::nullopt;
  } else if (pfds.revents & POLLIN) {
    uint64_t event_count;
    ssize_t read_bytes = read(eventfd_notify_, &event_count, sizeof(event_count));
    if (read_bytes != sizeof(event_count)) {
      std::cerr << "QEMUCXLConnector: Failed to read from eventfd: " << strerror(errno) << std::endl;
      return std::nullopt;
    }
    std::cout << "QEMUCXLConnector: Eventfd read successfully, value = " << event_count << std::endl;

    uint32_t irq_status = get_notification_status();
    std::cout << "IRQ status: 0x" << std::hex << irq_status << std::dec << std::endl;

    if (irq_status & IRQ_SOURCE_NEW_CLIENT_NOTIFY) {
      return check_for_new_client();
    } else if (irq_status & IRQ_SOURCE_CLOSE_CHANNEL_NOTIFY) {
      return check_for_closed_channel();
    }
  }
  return std::nullopt;
}

std::optional<CXLEventData> QEMUCXLConnector::check_for_new_client() {
  cxl_ipc_rpc_new_client_notify_t notify;
  recv_response(&notify, sizeof(notify));
  clear_notification_status(NOTIF_STATUS_NEW_CLIENT);

  CXLEventData event;
  event.type = CXLEvent::NEW_CLIENT_CONNECTED;
  event.connection = std::make_unique<QEMUConnection>(
    notify.channel_shm_offset,
    notify.channel_shm_size,
    notify.channel_id
  );

  return event;
}

std::optional<CXLEventData> QEMUCXLConnector::check_for_closed_channel() {
  cxl_ipc_rpc_close_channel_notify_t notify;
  recv_response(&notify, sizeof(notify));
  clear_notification_status(NOTIF_STATUS_CHANNEL_CLOSED);

  CXLEventData event;
  event.type = CXLEvent::CHANNEL_CLOSED;
  event.channel_id = notify.channel_id;

  return event;
}

};