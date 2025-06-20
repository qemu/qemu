#include "rpcclient.hpp"
#include <cstdint>
#include <exception>
#include <iostream>
#include <unistd.h>

int main() {
  const std::string device_path = "/dev/cxl_switch_client0";
  const std::string service_name = "TestService1";
  const std::string instance_id = "ClientInstance1";

  try {
    diancie::DiancieClient client(device_path, service_name, instance_id);
    // Test comms
    uint64_t test_offset = 0;
    uint64_t test_value = 0xABCDDCBAAAAABBBB;

    std::cout << "\nClient: Writing value 0x" << std::hex << test_value 
                  << " to offset " << std::dec << test_offset << " in the shared window." << std::endl;
    client.client_write_u64(test_offset, test_value);

    std::cout << "Client: Reading back from the same offset..." << std::endl;
    // In a better test scenario, the server would modify this value
    // For now, I just read what I wrote as a quick sanity test
    uint64_t read_value = client.client_read_u64(test_offset);

    std::cout << "Client: Read value 0x" << std::hex << read_value 
              << " from offset " << std::dec << test_offset << "." << std::endl;
    usleep(5000000);
    if (read_value == test_value) {
        std::cout << "Client: Read value matches written value. Test passed!" << std::endl;
    } else {
        std::cout << "Client: Read value does not match written value. Test failed!" << std::endl;
    }
  } catch (const std::exception& e) {
    std::cerr << "An error occurred: " << e.what() << std::endl;
  }

  std::cout << "DiancieClient main completed." << std::endl;
  return 0;
}