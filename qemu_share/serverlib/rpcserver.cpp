#include "rpcserver.hpp"
#include "../includes/cxl_switch_ipc.h"
#include "../includes/ioctl_defs.h"
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <unistd.h>

namespace diancie {

// --- Connection ---
Connection::Connection(int fd, uint64_t size)
  : fd_(fd), mapped_size_(size) {
  if (fd_ < 0 || mapped_size_ == 0) {
    throw std::invalid_argument("Invalid file descriptor or size for Connection");
  }
  mapped_base_ = mmap(nullptr, mapped_size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
  if (mapped_base_ == MAP_FAILED) {
    close(fd_);
    throw std::runtime_error("Failed to mmap connection base address: " + std::string(strerror(errno)));
  }
  std::cout << "Connection created. fd = " << fd_ 
            << ", mapped_base_ = " << mapped_base_ 
            << ", size = " << mapped_size_ << std::endl;
}

Connection::~Connection() {
  if (mapped_base_ != nullptr && mapped_base_ != MAP_FAILED) {
    munmap(mapped_base_, mapped_size_);
  }
  if (fd_ >= 0) {
    close(fd_);
  }
  std::cout << "Connection resources cleaned up. fd = " << fd_ 
            << ", mapped_base_ = " << mapped_base_ 
            << ", size = " << mapped_size_ << std::endl;
}

// --- DiancieServer ---
DiancieServer::DiancieServer(const std::string &device_path)
    : device_path_(device_path) {
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

  // 3. Setup event fds
  if (!setup_eventfd(eventfd_notify_, CXL_SWITCH_IOCTL_SET_EVENTFD_NOTIFY)) {
    munmap(bar1_base_, bar1_size_);
    munmap(bar0_base_, bar0_size_);
    close(device_fd_);
    throw std::runtime_error("Failed to setup eventfd for notifications");
  }

  if (!setup_eventfd(eventfd_cmd_ready_, CXL_SWITCH_IOCTL_SET_EVENTFD_CMD_READY)) {
    cleanup_eventfd(eventfd_notify_);
    munmap(bar1_base_, bar1_size_);
    munmap(bar0_base_, bar0_size_);
    close(device_fd_);
    throw std::runtime_error("Failed to setup eventfd for command ready");
  }

  std::cout << "Diancie server initialized. Device = " << device_path_
            << ", BAR0 mmapped at " << bar0_base_ << ", BAR1 mmapped at "
            << bar1_base_ << ", notify_efd " << eventfd_notify_
            << ", cmd_ready_efd " << eventfd_cmd_ready_ << std::endl;
}

DiancieServer::~DiancieServer() {
  std::cout << "Cleaning up Diancie server resources." << std::endl;
  cleanup_eventfd(eventfd_notify_);
  cleanup_eventfd(eventfd_cmd_ready_);

  if (bar1_base_ != MAP_FAILED && bar1_base_ != nullptr) {
    munmap(bar1_base_, bar1_size_);
  }

  if (bar2_base_ != MAP_FAILED && bar2_base_ != nullptr) {
    munmap(bar2_base_, bar2_size_);
  }

  if (device_fd_ >= 0) {
    close(device_fd_);
  }

  std::cout << "Diancie server resources cleaned up." << std::endl;
}

bool DiancieServer::setup_eventfd(int& efd_member, unsigned int ioctl_cmd) {
  efd_member = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
  if (efd_member < 0) {
    std::cerr << "Failed to create eventfd: " << strerror(errno) << std::endl;
    return false;
  }

  if (ioctl(device_fd_, ioctl_cmd, &efd_member) < 0) {
    std::cerr << "IOCTL failed to set eventfd (cmd 0x" << std::hex << ioctl_cmd << std::dec << "): " << strerror(errno) << std::endl;
    close(efd_member);
    efd_member = -1;
    return false;
  }

  return true;
}

void DiancieServer::cleanup_eventfd(int& efd_member) {
  if (efd_member >= 0) {
    close(efd_member);
    efd_member = -1;
  }
}

bool DiancieServer::register_service(const std::string& service_name, const std::string& instance_id) {
  cxl_ipc_rpc_register_service_req_t req;
  std::memset(&req, 0, sizeof(req));
  req.type = CXL_MSG_TYPE_RPC_REGISTER_SERVICE_REQ;
  
  strncpy(req.service_name, service_name.c_str(), MAX_SERVICE_NAME_LEN - 1);
  req.service_name[MAX_SERVICE_NAME_LEN-1] = '\0';

  strncpy(req.instance_id, instance_id.c_str(), MAX_INSTANCE_ID_LEN - 1);
  req.instance_id[MAX_INSTANCE_ID_LEN-1] = '\0';

  std::cout << "DiancieServer: Preparing to register service '" << req.service_name << "' with instance ID '" << req.instance_id << "'" << std::endl;

  // 1. Write request to BAR0 mailbox
  memcpy(bar0_base_, &req, sizeof(req));
  std::cout << "DiancieServer: Wrote registration request to BAR0." << std::endl;

  // 2. ring command doorbell in BAR1
  volatile uint32_t* doorbell_reg = static_cast<volatile uint32_t*>(
    static_cast<void*>(static_cast<char*>(bar1_base_) + REG_COMMAND_DOORBELL)
  );
  *doorbell_reg = 1; // write itself is the trigger
  std::cout << "DiancieServer: Rang command doorbell (wrote to BAR1+0x" << std::hex << REG_COMMAND_DOORBELL << ")." << std::dec << std::endl;

  // 3. wait for command response using eventfds
  std::cout << "DiancieServer: Waiting for response..." << std::endl;
  if (!wait_for_command_response(5000)) {
    std::cerr << "DiancieServer: Timeout waiting for command response." << std::endl;
    return false;
  }

  uint32_t final_cmd_status = get_command_status();
  std::cout << "DiancieServer: Command processing finished. Status: 0x" << std::hex << final_cmd_status << std::dec << std::endl;

  if (final_cmd_status == CMD_STATUS_RESPONSE_READY) {
    cxl_ipc_rpc_register_service_resp_t resp;
    memcpy(&resp, bar0_base_, sizeof(resp));
    std::cout << "DiancieServer: Received response from BAR0. Type: 0x" << std::hex << (int)resp.type
      << ", Status: 0x" << resp.status << std::dec << std::endl;
    return resp.status == CXL_IPC_STATUS_OK;
  } else {
    std::cerr << "DiancieServer: Command failed with status 0x" << std::hex << final_cmd_status << std::dec << std::endl;
    return false;
  }
}

bool DiancieServer::wait_for_command_response(int timeout_ms) {
  // Uses eventfd
  struct pollfd pfd;
  pfd.fd = eventfd_cmd_ready_;
  pfd.events = POLLIN;

  std::cout << "DiancieServer: Polling on command_ready_efd " << eventfd_cmd_ready_ << std::endl;
  int ret = poll(&pfd, 1, timeout_ms);

  if (ret < 0) {
    std::cerr << "DiancieServer: Poll error: " << strerror(errno) << std::endl;
    return false;
  } else if (ret == 0) {
    std::cerr << "DiancieServer: Poll timeout after " << timeout_ms << " ms." << std::endl;
    return false;
  } else if (pfd.revents & POLLIN) {
    uint64_t event_count;
    ssize_t read_bytes = read(eventfd_cmd_ready_, &event_count, sizeof(event_count));
    if (read_bytes != sizeof(event_count)) {
      std::cerr << "DiancieServer: Failed to read from eventfd: " << strerror(errno) << std::endl;
      return false;
    }
    std::cout << "DiancieServer: Eventfd read successfully, value = " << event_count << std::endl;
    return true;
  }

  std::cerr << "DiancieServer: Unexpected poll revents: " << pfd.revents << std::endl;
  return false;
}

cxl_ipc_rpc_new_client_notify_t DiancieServer::wait_for_new_client_notification(int timeout_ms) {
  struct pollfd pfd;
  pfd.fd = eventfd_notify_;
  pfd.events = POLLIN;
  
  std::cout << "DiancieServer: Polling on notify_efd " << eventfd_notify_ << std::endl;
  int ret = poll(&pfd, 1, timeout_ms);

  if (ret < 0) {
    std::cerr << "DiancieServer: Poll error: " << strerror(errno) << std::endl;
    throw std::runtime_error("Poll error");
  } else if (ret == 0) {
    std::cerr << "DiancieServer: Poll timeout after " << timeout_ms << " ms." << std::endl;
    throw std::runtime_error("Poll timeout");
  } else if (pfd.revents & POLLIN) {
    uint64_t event_count;
    ssize_t read_bytes = read(eventfd_notify_, &event_count, sizeof(event_count));
    if (read_bytes != sizeof(event_count)) {
      std::cerr << "DiancieServer: Failed to read from eventfd: " << strerror(errno) << std::endl;
      throw std::runtime_error("Failed to read from eventfd");
    }
    std::cout << "DiancieServer: Eventfd read successfully, value = " << event_count << std::endl;

    uint32_t notif_status = get_notification_status();
    std::cout << "DiancieServer: Notification status after eventfd read: 0x" << std::hex << notif_status << std::dec << std::endl;

    if (notif_status & NOTIF_STATUS_NEW_CLIENT) {
      cxl_ipc_rpc_new_client_notify_t notify;
      memcpy(&notify, bar0_base_, sizeof(notify));
      std::cout << "DiancieServer: Received NEW_CLIENT_NOTIFY for service '" 
                << notify.service_name << "' from client '" 
                << notify.client_instance_id << "'." << std::endl;
      clear_notification_status(NOTIF_STATUS_NEW_CLIENT);
      return notify;
    } else {
      std::cerr << "DiancieServer: No new client notification received." << std::endl;
      throw std::runtime_error("No new client notification");
    }
  }
  throw std::runtime_error("Poll indicated event but no POLLIN flag set!");
}

std::unique_ptr<Connection> DiancieServer::accept_connection(const cxl_ipc_rpc_new_client_notify_t& notif) {
  cxl_channel_map_info_t map_info;
  map_info.physical_offset = notif.channel_shm_offset;
  map_info.size = notif.channel_shm_size;

  std::cout << "DiancieServer: Accepting connection for service '" 
            << notif.service_name << "' with instance ID '" 
            << notif.client_instance_id << "'. Channel offset: 0x" 
            << std::hex << map_info.physical_offset 
            << ", size: 0x" << map_info.size << std::dec << std::endl;
  // Use the factory IOCTL to request a new fd
  if (ioctl(device_fd_, CXL_SWITCH_IOCTL_MAP_CHANNEL, &map_info) < 0) {
    std::cerr << "DiancieServer: IOCTL failed to map channel: " << strerror(errno) << std::endl;
    return nullptr;
  }
  // Return value is back inside the struct - hacky?
  int new_channel_fd;
  memcpy(&new_channel_fd, &map_info, sizeof(new_channel_fd));
  std::cout << "DiancieServer: IOCTL returned new channel fd: " << new_channel_fd << std::endl;
  if (new_channel_fd < 0) {
    std::cerr << "DiancieServer: Failed to create channel, IOCTL returned error fd: " << new_channel_fd << std::endl;
    return nullptr;
  }

  try {
    return std::make_unique<Connection>(new_channel_fd, notif.channel_shm_size);
  } catch (const std::exception& e) {
    std::cerr << "DiancieServer: Failed to create Connection: " << e.what() << std::endl;
    close(new_channel_fd);
    return nullptr;
  }
}



uint32_t DiancieServer::get_command_status() {
  volatile uint32_t* status_reg_ptr = static_cast<volatile uint32_t*>(
    static_cast<void*>(static_cast<char*>(bar1_base_) + REG_COMMAND_STATUS)
  );
  return *status_reg_ptr;
}

uint32_t DiancieServer::get_notification_status() {
  volatile uint32_t* status_reg_ptr = static_cast<volatile uint32_t*>(
    static_cast<void*>(static_cast<char*>(bar1_base_) + REG_NOTIF_STATUS)
  );
  return *status_reg_ptr;
}

void DiancieServer::clear_notification_status(uint32_t bits_to_clear) {
  volatile uint32_t* status_reg_ptr = static_cast<volatile uint32_t*>(
    static_cast<void*>(static_cast<char*>(bar1_base_) + REG_NOTIF_STATUS)
  );
  *status_reg_ptr = bits_to_clear;
  std::cout << "DiancieServer: Cleared notification status bits: 0x" << std::hex << bits_to_clear << std::dec << std::endl;
}
} // namespace diancie