#ifndef DIANCIE_RPC_CLIENT_HPP
#define DIANCIE_RPC_CLIENT_HPP

#include <cstdint>
#include <optional>
#include <string>

#include "../includes/cxl_switch_ipc.h"
#include "../includes/ioctl_defs.h"
#include "../includes/qemu_cxl_connector.hpp"

namespace diancie {

// The memory region that the client library is allocated
struct ChannelInfo {
  uint64_t offset;
  uint64_t size;
  uint64_t channel_id;
};

// The client library manages the low-level interactions for the user program.
// 1. Discover service and get allocated channel
class DiancieClient : protected QEMUCXLConnector {
public:
  DiancieClient() = delete;
  DiancieClient(const std::string& device_path, const std::string& service_name, const std::string& instance_id);
  ~DiancieClient();
  
  // For testing purposes, not final
  void client_write_u64(uint64_t offset, uint64_t value);
  uint64_t client_read_u64(uint64_t offset);


private:
  // For logging purposes
  std::string service_name_;
  std::string instance_id_;

  // For release
  uint64_t channel_id_;
  
  // Methods that are called by ctor and dtor
  // Find a service via the FM and request a comms channel
  std::optional<ChannelInfo> request_channel(const std::string& service_name, const std::string& instance_id);
  bool release_channel();
};

}

#endif