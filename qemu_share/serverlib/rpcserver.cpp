#include "rpcserver.hpp"
#include "../includes/cxl_switch_ipc.h"
#include "../includes/ioctl_defs.h"
#include <cstdint>
#include <cstring>
#include <exception>
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
Connection::Connection(uint64_t mapped_base, uint32_t size, uint64_t channel_id)
    : mapped_base_(mapped_base), mapped_size_(size), channel_id_(channel_id) {
  std::cout << "Connection created. channel_id_ = " << channel_id_ 
            << ", mapped_base_ = " << mapped_base_ 
            << ", size = " << mapped_size_ << std::endl;
}

// --- DiancieServer ---
DiancieServer::DiancieServer(const std::string &device_path, const std::string& service_name, const std::string& instance_id)
    : device_path_(device_path), service_name_(service_name), instance_id_(instance_id) {
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

  // 4. Register service
  if (!register_service()) {
    cleanup_eventfd(eventfd_cmd_ready_);
    cleanup_eventfd(eventfd_notify_);
    munmap(bar2_base_, bar2_size_);
    munmap(bar1_base_, bar1_size_);
    munmap(bar0_base_, bar0_size_);
    close(device_fd_);
    throw std::runtime_error("Failed to register service!");
  }

  std::cout << "Diancie server initialized. Device = " << device_path_
            << ", BAR0 mmapped at " << bar0_base_ << ", BAR1 mmapped at "
            << bar1_base_ << ", BAR2 mapped at " << bar2_base_ << ", notify_efd " << eventfd_notify_
            << ", cmd_ready_efd " << eventfd_cmd_ready_ << std::endl;
}

DiancieServer::~DiancieServer() {
  std::cout << "Cleaning up Diancie server resources." << std::endl;
  deregister_service();
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

// TODO: this server should also register the actual function
bool DiancieServer::register_service() {
  cxl_ipc_rpc_register_service_req_t req;
  std::memset(&req, 0, sizeof(req));
  req.type = CXL_MSG_TYPE_RPC_REGISTER_SERVICE_REQ;
  
  strncpy(req.service_name, service_name_.c_str(), MAX_SERVICE_NAME_LEN - 1);
  req.service_name[MAX_SERVICE_NAME_LEN-1] = '\0';

  strncpy(req.instance_id, instance_id_.c_str(), MAX_INSTANCE_ID_LEN - 1);
  req.instance_id[MAX_INSTANCE_ID_LEN-1] = '\0';

  std::cout << "DiancieServer: Preparing to register service '" << req.service_name << "' with instance ID '" << req.instance_id << "'" << std::endl;

  if (send_command(&req, sizeof(req))) {
    cxl_ipc_rpc_register_service_resp_t resp;
    memcpy(&resp, bar0_base_, sizeof(resp));
    return resp.status == CXL_IPC_STATUS_OK;
  }

  return false;
}

void DiancieServer::deregister_service() {
  std::cout << "DiancieServer: Deregistering service '" << service_name_ << "' with instance ID '" << instance_id_ << "'" << std::endl;
  cxl_ipc_rpc_deregister_service_req_t req;
  req.type = CXL_MSG_TYPE_RPC_DEREGISTER_SERVICE_REQ;
  strncpy(req.service_name, service_name_.c_str(), MAX_SERVICE_NAME_LEN - 1);
  req.service_name[MAX_SERVICE_NAME_LEN-1] = '\0';

  strncpy(req.instance_id, instance_id_.c_str(), MAX_INSTANCE_ID_LEN - 1);
  req.instance_id[MAX_INSTANCE_ID_LEN-1] = '\0';

  if (send_command(&req, sizeof(req))) {
    cxl_ipc_rpc_deregister_service_resp_t resp;
    memcpy(&resp, bar0_base_, sizeof(resp));
    if (resp.status == CXL_IPC_STATUS_OK) {
      std::cout << "DiancieServer: Service '" << service_name_ << "' deregistered successfully." << std::endl;
    }
  } else {
    std::cerr << "DiancieServer: Failed to send deregister command." << std::endl;
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

bool DiancieServer::send_command(const void* req, size_t size) {
  memcpy(bar0_base_, req, size);
  volatile uint32_t* doorbell = static_cast<volatile uint32_t*>(static_cast<void*>(static_cast<char*>(bar1_base_) + REG_COMMAND_DOORBELL));
  *doorbell = 1;
  if (!wait_for_command_response(5000)) {
    std::cerr << "Timeout waiting for command response." << std::endl;
    return false;
  }

  return get_command_status() == CMD_STATUS_RESPONSE_READY;
}

Connection DiancieServer::wait_for_new_client_notification(int timeout_ms) {
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
      
      return Connection(notify.channel_shm_offset, notify.channel_shm_size, notify.channel_id);
    } else {
      std::cerr << "DiancieServer: No new client notification received." << std::endl;
      throw std::runtime_error("No new client notification");
    }
  }
  throw std::runtime_error("Poll indicated event but no POLLIN flag set!");
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

// Runs in a dedicated thread for each client connection
// Poll request and do mapping
// TODO: Figure out template metaprogramming to parse the struct
void DiancieServer::service_client(Connection connection) {
  std::cout << "Servicing client on channel id " << connection.channel_id_ 
            << " with base address " << connection.mapped_base_ 
            << " and size " << connection.mapped_size_ << std::endl;
  // bool client_is_active = true;

  // uint64_t bar2_addr = reinterpret_cast<uint64_t>(bar2_base_);
  // std::cout << "BAR2 base addr is " << std::hex << bar2_addr << std::dec << std::endl;
  // std::cout << "bar2 addr is " << std::hex << bar2_addr << std::dec << std::endl;
  
  // uint64_t act_addr = bar2_addr + connection.mapped_base_;
  // std::cout << "act addr is " << std::hex << act_addr << std::dec << std::endl;

  // volatile uint64_t* addr = reinterpret_cast<volatile uint64_t*>(act_addr);
  //   // THIS IS THE CRITICAL CHANGE FOR PRINTING:
  // std::cout << "Addr is " << std::hex << reinterpret_cast<uintptr_t>(addr) << std::dec
  //           << " on channel id " << connection.channel_id_ << std::endl;

  
  volatile uint64_t* addr = static_cast<volatile uint64_t*>(static_cast<void*>(static_cast<char*>(bar2_base_) + connection.mapped_base_));
  
  std::cout << "Value is " << std::hex << *addr << std::dec << std::endl;

  // // Add the offset to the BAR2 base pointer
  // char* final_ptr = bar2_char_ptr + connection.mapped_base_;
  // std::cout << "final_ptr is " << &final_ptr << std::endl;
  // volatile uint64_t* addr = reinterpret_cast<volatile uint64_t*>(final_ptr);
  
  // // uint64_t bar_2_base_addr = reinterpret_cast<uint64_t>(bar2_base_);
  // // std::cout << "bar 2 base addr is " << bar_2_char_ptr << "\n";
  // // std::cout << "connection mapped base is " << connection.mapped_base_ << "\n";
  // // uint64_t final_base_addr = bar_2_base_addr + connection.mapped_base_;
  // // std::cout << "final base addr is " << final_base_addr << "\n";
  // // volatile uint64_t* addr = reinterpret_cast<volatile uint64_t*>(static_cast<uintptr_t>(final_base_addr));
  // std::cout << "Addr is " << addr << " on channel id " << connection.channel_id_ << std::endl;
  // while (client_is_active) {
  //   // TODO: Add parsing of struct
  //   // For now, simple printing of value as from concurrent
  //   uint64_t recv_value = 0;
  //   std::cout << "Polling from addr " << std::hex << addr << std::dec 
  //             << " on channel id " << connection.channel_id_ << std::endl;
  //   while ((recv_value = addr[0]) == 0) {
  //     usleep(10000000);
  //   }
  //   std::cout << "Read 0x" << std::hex << recv_value << std::dec 
  //             << " from client on channel id " << connection.channel_id_ << std::endl;
  // }
}

void DiancieServer::run_server_loop() {
  // TODO: Max 5 for now. Handle properly later
  int num_clients = 0;
  bool running = true;
  while (running) {
    try {
      auto connection_info = wait_for_new_client_notification(-1);
      clients_.emplace_back(&DiancieServer::service_client, this, connection_info);
      num_clients++;
      if (num_clients == 5) {
        // Hacky sleep for 10 seconds to let client resolve
        // TODO: Proper handle later
        usleep(10000000);
        running = false;
      }
    } catch (const std::exception& e) {
      std::cerr << "DiancieServer: " << e.what() << std::endl;
    }
  }
  for (auto &t : clients_) {
    t.join();
  }
}

} // namespace diancie