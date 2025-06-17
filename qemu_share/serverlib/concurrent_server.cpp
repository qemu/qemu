#include "../includes/cxl_switch_ipc.h"
#include "rpcserver.hpp"
#include <exception>
#include <ios>
#include <iostream>
#include <stdexcept>
#include <unistd.h>
#include <vector>
#include <thread>

void handle_client(std::unique_ptr<diancie::Connection> connection, uint64_t channel_id) {
    std::cout << "[Thread " << channel_id << "]: Handling client connection." << std::endl;
    volatile uint64_t* data = static_cast<volatile uint64_t*>(connection->get_base_address());
    
    // wait for client to write a value, then echo it back
    uint64_t received_value = 0;
    while( (received_value = data[0]) == 0) {
         usleep(100000);
    }
    std::cout << "[Thread " << channel_id << "]: Read 0x" << std::hex << received_value << std::dec << std::endl;
    data[1] = received_value + 1; // Write back a response
    std::cout << "[Thread " << channel_id << "]: Wrote response. Exiting." << std::endl;
}

int main() {
  std::string device_path = "/dev/cxl_switch_client0";
  std::string service_name = "TestService1";
  std::string instance_id = "TestInstance1";

  try {
    diancie::DiancieServer server(device_path, service_name, instance_id);
    std::vector<std::thread> client_threads;
    constexpr int num_clients = 5;
    for (int i = 0; i < num_clients; i++) {
      std::cout << "[Main]: Waiting for client " << i+1 << std::endl;
      auto notif = server.wait_for_new_client_notification(100000);
      auto connection = server.accept_connection(notif);
      if (connection) {
        client_threads.emplace_back(handle_client, std::move(connection), notif.channel_id);
      }
    }

    for (auto &t : client_threads) {
      t.join();
    }
  } catch (const std::exception& e) {
    std::cerr << "An error occurred: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}