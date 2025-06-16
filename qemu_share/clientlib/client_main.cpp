#include "rpcclient.hpp"
#include <cstdint>
#include <exception>
#include <iostream>

int main() {
  const char* device_path = "/dev/cxl_switch_client0";

  try {
    diancie::DiancieClient client(device_path);

    std::cout << "DiancieClient initialized successfully." << std::endl;
    auto channel_info = client.request_channel("TestService1", "ClientInstance1");

    if (channel_info) {
        std::cout << "Channel requested successfully!" << std::endl;
        std::cout << "  --> Channel Offset: 0x" << std::hex << channel_info->offset << std::dec << std::endl;
        std::cout << "  --> Channel Size:   " << channel_info->size << " bytes" << std::endl;
        std::cout << "\nClient: Setting local BAR2 memory window..." << std::endl;
        if (client.set_memory_window(channel_info->offset, channel_info->size)) {
            std::cout << "Client: BAR2 memory window set successfully!" << std::endl;
            // Test comms
            uint64_t test_offset = 0;
            uint64_t test_value = 0xDEADBEEFCAFEBABE;

            std::cout << "\nClient: Writing value 0x" << std::hex << test_value 
                          << " to offset " << std::dec << test_offset << " in the shared window." << std::endl;
            client.write_u64(test_offset, test_value);

            std::cout << "Client: Reading back from the same offset..." << std::endl;
            // In a better test scenario, the server would modify this value
            // For now, I just read what I wrote as a quick sanity test
            uint64_t read_value = client.read_u64(test_offset);

            std::cout << "Client: Read value 0x" << std::hex << read_value 
                      << " from offset " << std::dec << test_offset << "." << std::endl;
            
            if (read_value == test_value) {
                std::cout << "Client: Read value matches written value. Test passed!" << std::endl;
            } else {
                std::cout << "Client: Read value does not match written value. Test failed!" << std::endl;
            }
        } else {
            std::cerr << "Client: Failed to set BAR2 memory window." << std::endl;
        }
    } else {
        std::cerr << "Client: Failed to request channel." << std::endl;
    }
  } catch (const std::exception& e) {
    std::cerr << "An error occurred: " << e.what() << std::endl;
  }

  std::cout << "DiancieClient main completed." << std::endl;
  return 0;
}