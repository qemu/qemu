#include "rpcserver.hpp"
#include "../includes/a_cxl_connector.hpp"
#include "../includes/cxl_switch_ipc.h"
#include "../includes/ioctl_defs.h"
#include "../includes/qemu_cxl_connector.hpp"
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

// --- DiancieServer ---
DiancieServer::DiancieServer(const std::string &device_path, const std::string& service_name, const std::string& instance_id)
    : QEMUCXLConnector(device_path), service_name_(service_name), instance_id_(instance_id) {
  // QEMUCXLConnector's constructor will initialize rest of stuff
  if (!register_service()) {
    throw std::runtime_error("Failed to register service!");
  }

  std::cout << "DiancieServer initialized" << std::endl;
}

DiancieServer::~DiancieServer() {
  std::cout << "Cleaning up Diancie server resources." << std::endl;
  deregister_service();
  std::cout << "Diancie server resources cleaned up." << std::endl;
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
    recv_response(&resp, sizeof(resp));
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
    recv_response(&resp, sizeof(resp));
    if (resp.status == CXL_IPC_STATUS_OK) {
      std::cout << "DiancieServer: Service '" << service_name_ << "' deregistered successfully." << std::endl;
    }
  } else {
    std::cerr << "DiancieServer: Failed to send deregister command." << std::endl;
  }
}


std::unique_ptr<AbstractCXLConnection> DiancieServer::wait_for_new_client_notification(int timeout_ms) {
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
      recv_response(&notify, sizeof(notify));
      std::cout << "DiancieServer: Received NEW_CLIENT_NOTIFY for service '" 
                << notify.service_name << "' from client '" 
                << notify.client_instance_id << "'." << std::endl;
      clear_notification_status(NOTIF_STATUS_NEW_CLIENT);
      
      return std::make_unique<QEMUConnection>(notify.channel_shm_offset, notify.channel_shm_size, notify.channel_id);
    } else {
      std::cerr << "DiancieServer: No new client notification received." << std::endl;
      throw std::runtime_error("No new client notification");
    }
  }
  throw std::runtime_error("Poll indicated event but no POLLIN flag set!");
}

// Runs in a dedicated thread for each client connection
// Poll request and do mapping
// TODO: Figure out template metaprogramming to parse the struct
void DiancieServer::service_client(QEMUConnection connection) {
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

  uint64_t value = read_u64(connection.mapped_base_);
  std::cout << "Value is " << std::hex << value << std::dec << std::endl;

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
      // Server now needs to set memory window too so that its QEMU device knows
      // how to talk to memory. This breaks the abstraction barrier and so
      // will be rewritten in the future.
      if (auto qemu_conn = dynamic_cast<QEMUConnection*>(connection_info.get())) {
        set_memory_window(qemu_conn->get_base(), qemu_conn->get_size(), qemu_conn->get_channel_id());
        clients_.emplace_back(&DiancieServer::service_client, this, *qemu_conn);
      }
            
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