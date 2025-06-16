#include "../includes/cxl_switch_ipc.h"
#include "rpcserver.hpp"
#include <exception>
#include <ios>
#include <iostream>
#include <stdexcept>

int main() {
  const char *device_path = "/dev/cxl_switch_client0";

  try {
    diancie::DiancieServer server(device_path);

    std::string service_name = "TestService1";
    std::string instance_id = "TestInstance1";

    std::cout << "Attempt to register service: " << service_name
              << " with instance id " << instance_id << std::endl;

    if (server.register_service(service_name, instance_id)) {
      std::cout << "Service " << service_name << " was registered."
                << std::endl;
      std::cout << "Now trying to wait for a client notification..."
                << std::endl;

      try {
        // Wait 30 seconds
        cxl_ipc_rpc_new_client_notify_t notification =
            server.wait_for_new_client_notification(30000);
        std::cout << "Received notification for client!" << std::endl;
        std::cout << "  Service Name: " << notification.service_name
                  << std::endl;
        std::cout << "  Client Instance ID: " << notification.client_instance_id
                  << std::endl;
        std::cout << "  Channel SHM Offset: 0x" << std::hex
                  << notification.channel_shm_offset << std::dec << std::endl;
        std::cout << "  Channel SHM Size: " << notification.channel_shm_size
                  << " bytes" << std::endl;
      } catch (const std::runtime_error &e) {
        std::cerr << "Error waiting for client notification: " << e.what()
                  << std::endl;
      }
    } else {
      std::cerr << "Failed to register service " << service_name << "."
                << std::endl;
      std::cerr << "Final command status from device: 0x" << std::hex
                << server.get_command_status() << std::dec << std::endl;
    }
  } catch (const std::exception& e) {
    std::cerr << "An error occurred: " << e.what() << std::endl;
    return 1; // Non-zero exit code for errors
  }

  return 0;
}