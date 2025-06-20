#include "rpcclient.hpp"
#include "../includes/ioctl_defs.h"
#include "../includes/cxl_switch_ipc.h"
#include "../includes/qemu_cxl_connector.hpp"
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
    : QEMUCXLConnector(device_path), service_name_(service_name), instance_id_(instance_id) {
  // 4. Request channel
  auto channel_info = request_channel(service_name_, instance_id_);
  if (!channel_info.has_value()) {
    throw std::runtime_error("Failed to request channel!");
  }

  std::cout << "Channel id from channel info is " << channel_info->channel_id;
  channel_id_ = channel_info->channel_id;
  std::cout << "Channel id_ is " << channel_id_ << std::endl;

  // 5. Map mem window
  if (!set_memory_window(channel_info->offset, channel_info->size)) {
    release_channel();
    throw std::runtime_error("Failed to set memory window for channel!");
  }

  std::cout << "DiancieClient initialized." << std::endl;
}

DiancieClient::~DiancieClient() {
  release_channel();
  std::cout << "DiancieClient resources cleaned up." << std::endl;
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
    recv_response(&resp, sizeof(resp));
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
    recv_response(&resp, sizeof(resp));
    return resp.status == CXL_IPC_STATUS_OK;
  }
  return false;
}

void DiancieClient::client_write_u64(uint64_t offset, uint64_t value) {
  QEMUCXLConnector::write_u64(offset, value);
}

uint64_t DiancieClient::client_read_u64(uint64_t offset) {
  return QEMUCXLConnector::read_u64(offset);
}



} // namespace diancie
