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
  for (auto& [channel_id, thread] : clients_) {
    if (thread.joinable()) {
      thread.join();
    }
  }
  clients_.clear();
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

// Runs in a dedicated thread for each client connection
// Poll request and do mapping
// TODO: Figure out template metaprogramming to parse the struct
void DiancieServer::service_client(std::unique_ptr<AbstractCXLConnection> connection) {
  std::cout << "Servicing client on channel id " << connection->get_channel_id()
            << " with base address " << connection->get_base()
            << " and size " << connection->get_size() << std::endl;
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

  uint64_t value = read_u64(connection->get_base());
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

void DiancieServer::handle_new_client(std::unique_ptr<AbstractCXLConnection> conn) {
  set_memory_window(conn->get_base(), conn->get_size(), conn->get_channel_id());
  uint64_t channel_id = conn->get_channel_id();
  
  std::cout << "New client connected with channel ID: " << conn->get_channel_id() 
            << ", Base: " << conn->get_base() 
            << ", Size: " << conn->get_size() << std::endl;

  clients_[channel_id] = std::thread([this, conn = std::move(conn)]() mutable {
    this->service_client(std::move(conn));
  });
}

// At the moment, both channel close and client disconnect seem suspicously the same
void DiancieServer::handle_channel_close(uint64_t channel_id) {
  std::cout << "Channel with ID " << channel_id << " is being closed." << std::endl;

  auto it = clients_.find(channel_id);
  if (it != clients_.end()) {
    if (it->second.joinable()) {
      it->second.join();
    }
    clients_.erase(it);
    std::cout << "Channel with ID " << channel_id << " closed successfully." << std::endl;
  } else {
    std::cerr << "Channel with ID " << channel_id << " not found." << std::endl;
  }
}

void DiancieServer::handle_client_disconnect(uint64_t channel_id) {
  std::cout << "Client with channel ID " << channel_id << " disconnected." << std::endl;
  
  auto it = clients_.find(channel_id);
  if (it != clients_.end()) {
    if (it->second.joinable()) {
      it->second.join();
    }
    clients_.erase(it);
  }
}

void DiancieServer::run_server_loop() {
  bool running = true;
  while (running) {
    try {
      auto event_data = wait_for_event(1000);

      if (!event_data) {
        continue;
      }

      switch (event_data->type) {
      case CXLEvent::NEW_CLIENT_CONNECTED:
        std::cout << "Received New client connected event from event loop" << std::endl;
        handle_new_client(std::move(event_data->connection));
        break;
      case CXLEvent::CHANNEL_CLOSED:
        std::cout << "Received Channel closed event from event loop" << std::endl;
        handle_channel_close(event_data->channel_id);
        break;
      case CXLEvent::CLIENT_DISCONNECTED:
        std::cout << "Received Client disconnected event from event loop" << std::endl;
        handle_client_disconnect(event_data->channel_id);
        break;
      case CXLEvent::COMMAND_RECEIVED:
        break;
      case CXLEvent::ERROR_OCURRED:
        break;
      default:
        std::cout << "DiancieServer: Unknown event type" << std::endl;
        break;
      }
    } catch (const std::exception& e) {
      std::cerr << "DiancieServer: " << e.what() << std::endl;
    }
  }
}

} // namespace diancie