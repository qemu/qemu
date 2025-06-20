#include "rpcclient.hpp"
#include "../includes/ioctl_defs.h"
#include "../includes/cxl_switch_ipc.h"
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <type_traits>
#include <unistd.h>

namespace diancie {
DiancieClient::DiancieClient(const std::string &device_path, const std::string &service_name, const std::string &instance_id)
    : device_path_(device_path), service_name_(service_name), instance_id_(instance_id) {
  // 1. Open device fd
  device_fd_ = open(device_path_.c_str(), O_RDWR);
  if (device_fd_ < 0) {
    throw std::runtime_error("Failed to open device: " + device_path_);
  }

  // 2. Map bars
  bar0_size_ = DEFAULT_BAR_SIZE;
  bar0_base_ = mmap(nullptr, bar0_size_, PROT_READ | PROT_WRITE, MAP_SHARED,
                    device_fd_, BAR0_MMAP_OFFSET);
  if (bar0_base_ == MAP_FAILED) {
    close(device_fd_);
    throw std::runtime_error("Failed to mmap BAR0: " + device_path_);
  }

  bar1_size_ = DEFAULT_BAR_SIZE;
  bar1_base_ = mmap(nullptr, bar1_size_, PROT_READ | PROT_WRITE, MAP_SHARED,
                    device_fd_, BAR1_MMAP_OFFSET);
  if (bar1_base_ == MAP_FAILED) {
    munmap(bar0_base_, bar0_size_);
    close(device_fd_);
    throw std::runtime_error("Failed to mmap BAR1: " + device_path_);
  }

  bar2_size_ = DEFAULT_BAR2_SIZE;
  bar2_base_ = mmap(nullptr, bar2_size_, PROT_READ | PROT_WRITE, MAP_SHARED, device_fd_, BAR2_MMAP_OFFSET);
  if (bar2_base_ == MAP_FAILED) {
    munmap(bar1_base_, bar1_size_);
    munmap(bar0_base_, bar0_size_);
    close(device_fd_);
    throw std::runtime_error("Failed to mmap BAR2: " + device_path_);
  }

  // 3. Setup event fds
  if (!setup_eventfd(eventfd_cmd_ready_, CXL_SWITCH_IOCTL_SET_EVENTFD_CMD_READY)) {
    munmap(bar1_base_, bar1_size_);
    munmap(bar0_base_, bar0_size_);
    close(device_fd_);
    throw std::runtime_error("Failed to setup eventfd for notifications");
  }
  
  // 4. Request channel
  auto channel_info = request_channel(service_name_, instance_id_);
  if (!channel_info.has_value()) {
    munmap(bar1_base_, bar1_size_);
    munmap(bar0_base_, bar0_size_);
    close(device_fd_);
    throw std::runtime_error("Failed to request channel!");
  }

  std::cout << "Channel id from channel info is " << channel_info->channel_id;
  channel_id_ = channel_info->channel_id;
  std::cout << "Channel id_ is " << channel_id_ << std::endl;

  // 5. Map mem window
  if (!set_memory_window(channel_info->offset, channel_info->size)) {
    release_channel();
    munmap(bar1_base_, bar1_size_);
    munmap(bar0_base_, bar0_size_);
    close(device_fd_);
    throw std::runtime_error("Failed to set memory window for channel!");
  }

  std::cout << "DiancieClient initialized. Device = " << device_path_
            << ", BAR0 mmapped at " << bar0_base_ << ", BAR1 mmapped at "
            << bar1_base_ << "BAR2 mmapped at " << bar2_base_ << ", cmd_ready_efd " << eventfd_cmd_ready_ << std::endl;
}

DiancieClient::~DiancieClient() {
  release_channel();
  if (bar0_base_ != MAP_FAILED && bar0_base_ != nullptr) {
    munmap(bar0_base_, bar0_size_);
  }
  if (bar1_base_ != MAP_FAILED && bar1_base_ != nullptr) {
    munmap(bar1_base_, bar1_size_);
  }
  if (bar2_base_ != MAP_FAILED && bar2_base_ != nullptr) {
    munmap(bar2_base_, bar2_size_);
  }
  if (device_fd_ >= 0) {
    close(device_fd_);
  }

  if (eventfd_cmd_ready_ >= 0) {
    close(eventfd_cmd_ready_);
    eventfd_cmd_ready_ = -1;
  }
  std::cout << "DiancieClient resources cleaned up." << std::endl;
}

bool DiancieClient::setup_eventfd(int& efd_member, unsigned int ioctl_cmd) {
  efd_member = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
  if (efd_member < 0) return false;
  if (ioctl(device_fd_, ioctl_cmd, &efd_member) < 0) {
    close(efd_member);
    efd_member = -1;
    return false;
  }
  return true;
}

uint32_t DiancieClient::get_command_status() {
  volatile uint32_t* status_reg = static_cast<volatile uint32_t*>(static_cast<void*>(static_cast<char*>(bar1_base_) + REG_COMMAND_STATUS));
  return *status_reg;
}

bool DiancieClient::wait_for_command_response(int timeout_ms) {
  struct pollfd pfd;
  pfd.fd = eventfd_cmd_ready_;
  pfd.events = POLLIN;
  int ret = poll(&pfd, 1, timeout_ms);
  if (ret > 0 && (pfd.revents & POLLIN)) {
    uint64_t event_count;
    read(eventfd_cmd_ready_, &event_count, sizeof(event_count));
    return true;
  }
  return false;
}

bool DiancieClient::send_command(const void* req, size_t size) {
  memcpy(bar0_base_, req, size);
  volatile uint32_t* doorbell = static_cast<volatile uint32_t*>(static_cast<void*>(static_cast<char*>(bar1_base_) + REG_COMMAND_DOORBELL));
  *doorbell = 1;
  if (!wait_for_command_response(5000)) {
    std::cerr << "Timeout waiting for command response." << std::endl;
    return false;
  }

  return get_command_status() == CMD_STATUS_RESPONSE_READY;
}

std::optional<ChannelInfo> DiancieClient::request_channel(const std::string &service_name, const std::string &instance_id) {
  cxl_ipc_rpc_request_channel_req_t req;
  std::memset(&req, 0, sizeof(req));
  req.type = CXL_MSG_TYPE_RPC_REQUEST_CHANNEL_REQ;
  strncpy(req.service_name, service_name.c_str(), MAX_SERVICE_NAME_LEN - 1);
  strncpy(req.instance_id, instance_id.c_str(), MAX_INSTANCE_ID_LEN - 1);

  std::cout << "DiancieClient: Requesting channel for service '" 
            << req.service_name << "' with instance ID '" 
            << req.instance_id << "'" << std::endl;

  if (send_command(&req, sizeof(req))) {
    cxl_ipc_rpc_request_channel_resp_t resp;
    memcpy(&resp, bar0_base_, sizeof(resp));
    if (resp.status == CXL_IPC_STATUS_OK) {
      std::cout << "DiancieClient: Channel allocated successfully. "
                << "Offset: 0x" << std::hex << resp.channel_shm_offset 
                << ", Size: 0x" << resp.channel_shm_size 
                << ", Channel ID: " << resp.channel_id << std::dec << std::endl;
      return ChannelInfo{resp.channel_shm_offset, resp.channel_shm_size, resp.channel_id};
    }
  }
  return std::nullopt;
}

bool DiancieClient::release_channel() {
  cxl_ipc_rpc_release_channel_req_t req;
  req.type = CXL_MSG_TYPE_RPC_RELEASE_CHANNEL_REQ;
  req.channel_id = channel_id_;

  if (send_command(&req, sizeof(req))) {
    cxl_ipc_rpc_release_channel_resp_t resp;
    memcpy(&resp, bar0_base_, sizeof(resp));
    return resp.status == CXL_IPC_STATUS_OK;
  }
  return false;
}

bool DiancieClient::set_memory_window(uint64_t offset, uint64_t size) {
  cxl_ipc_rpc_set_bar2_window_req_t req;
  std::memset(&req, 0, sizeof(req));
  req.type = CXL_MSG_TYPE_RPC_SET_BAR2_WINDOW_REQ;
  req.offset = offset;
  req.size = size;

  std::cout << "Sending mem window request " 
            << "Offset: 0x" << std::hex << req.offset 
            << ", Size: 0x" << req.size << std::dec << std::endl;


  if (send_command(&req, sizeof(req))) {
    cxl_ipc_rpc_set_bar2_window_resp_t resp;
    memcpy(&resp, bar0_base_, sizeof(resp));
    return resp.status == CXL_IPC_STATUS_OK;
  }

  return false;
}

void DiancieClient::write_u64(uint64_t offset, uint64_t value) {
  if (bar2_base_ == nullptr) {
    throw std::runtime_error("BAR2 memory window not set. Call set_memory_window() first.");
  }
  if (offset + sizeof(value) > bar2_size_) {
    throw std::out_of_range("Write out of bounds of BAR2 memory window.");
  }
  volatile uint64_t* addr = static_cast<volatile uint64_t*>(static_cast<void*>(static_cast<char*>(bar2_base_) + offset));
  *addr = value;
}

uint64_t DiancieClient::read_u64(uint64_t offset) {
  if (bar2_base_ == nullptr) {
    throw std::runtime_error("BAR2 memory window not set. Call set_memory_window() first.");
  }
  if (offset + sizeof(uint64_t) > bar2_size_) {
    throw std::out_of_range("Read out of bounds of BAR2 memory window.");
  }
  volatile uint64_t* addr = static_cast<volatile uint64_t*>(static_cast<void*>(static_cast<char*>(bar2_base_) + offset));
  return *addr;
}

} // namespace diancie
