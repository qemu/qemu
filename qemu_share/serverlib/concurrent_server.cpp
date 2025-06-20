#include "../includes/cxl_switch_ipc.h"
#include "rpcserver.hpp"
#include <exception>
#include <ios>
#include <iostream>
#include <stdexcept>
#include <unistd.h>
#include <vector>
#include <thread>

int main() {
  std::string device_path = "/dev/cxl_switch_client0";
  std::string service_name = "TestService1";
  std::string instance_id = "TestInstance1";

  try {
    diancie::DiancieServer server(device_path, service_name, instance_id);
    server.run_server_loop();
  } catch (const std::exception& e) {
    std::cerr << "An error occurred: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}