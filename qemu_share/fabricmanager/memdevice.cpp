#include "memdevice.hpp"
#include "../includes/cxl_switch_ipc.h"
#include "cxl_fm.hpp"

#include <exception>
#include <fcntl.h>
#include <iostream>
#include <cstdint>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace cxl_fm {

#define CXL_MEMDEV_DEBUG 1
#if CXL_MEMDEV_DEBUG
#define CXL_MEMDEV_LOG(msg) do { std::cerr << "CXL MemDev: " << msg << std::endl; } while(0)
#define CXL_MEMDEV_LOG_ERR(msg, err_no) do { std::cerr << "CXL MemDev Error: " << msg << ", errno: " << strerr(err_no) << std::endl; } while(0)
#else
#define CXLCXL_MEMDEV_LOG(msg)
#define CXL_MEMDEV_LOG_ERR(msg, err_no)
#endif

// --- Memory management ---

void CXLMemDevice::add_new_block(size_t offset, size_t size) {
  // Implicit conversion from size_t to FreeBlockInfo done here
  auto new_block_it = m_free_blocks_by_offset_.emplace(offset, size);
  auto order_it = m_free_blocks_by_size_.emplace(size, new_block_it.first);
  new_block_it.first->second.ordered_by_size_it = order_it;
}

std::optional<size_t> CXLMemDevice::allocate(size_t requested_size) {
  CXL_MEMDEV_LOG("Requesting " + std::to_string(requested_size) + " bytes");
  
  if(free_size_ < requested_size) {
    CXL_MEMDEV_LOG("Free size (" + std::to_string(free_size_) + " bytes) is less than requested size (" + std::to_string(requested_size) + " bytes)");
    return std::nullopt;
  }

  // Get the first free block that is large enough to accommodate the requested size
  auto smallest_block_itit = m_free_blocks_by_size_.lower_bound(requested_size);
  if (smallest_block_itit == m_free_blocks_by_size_.end()) {
    CXL_MEMDEV_LOG("Could not get a free block large enough");
    return std::nullopt;
  }

  auto smallest_block_it = smallest_block_itit->second;
  auto offset = smallest_block_it->first;
  auto new_offset = offset + requested_size;
  auto new_size = smallest_block_it->second.size - requested_size;
  // Remove the old block from both maps
  m_free_blocks_by_size_.erase(smallest_block_itit);
  m_free_blocks_by_offset_.erase(smallest_block_it);
  
  if (new_size > 0)
    add_new_block(new_offset, new_size);

  free_size_ -= requested_size;
  return std::optional<size_t>(offset);
}

void CXLMemDevice::free(size_t offset, size_t size) {
    // First, we want to know where the new block should be inserted
    // We will find the first element whose offset is greater than the 
    // specified offset
    std::cout << "MemDevice freeing memory at offset " << offset << " and size " << size << std::endl;
    // We also zero out the data first. This makes debugging easier. But might
    // be removed subsequently.
    zero_memory_region(offset, size);
    auto next_block_it = m_free_blocks_by_offset_.upper_bound(offset);
    auto prev_block_it = next_block_it;
    if (prev_block_it != m_free_blocks_by_offset_.begin()) {
        --prev_block_it; // Move to the previous block
    } else {
        prev_block_it = m_free_blocks_by_offset_.end(); // No previous block
    }
    // From here, there are 4 cases
    // 1. Block being released is not adjacent to any other free block.
    //    We will add it as a new free block
    // 2. Block being released is adjacent to the previous block. Merge them.
    // 3. Block being released is adjacent to the next block. Merge them.
    // 4. Block being released is adjacent to both previous and next blocks. 
    //    Merge all three.
    size_t new_size, new_offset;
    if (prev_block_it != m_free_blocks_by_offset_.end() &&
        offset == prev_block_it->first + prev_block_it->second.size) {
      // The block being released is adjacent to the previous block. Merge them
      new_size = prev_block_it->second.size + size;
      new_offset = prev_block_it->first;
      // Is it also adjacent to the next block?
      if (next_block_it != m_free_blocks_by_offset_.end() && offset + size == next_block_it->first) {
        // It is adjacent to the next block. Merge the next block as well
        new_size += next_block_it->second.size;
        m_free_blocks_by_size_.erase(prev_block_it->second.ordered_by_size_it);
        m_free_blocks_by_size_.erase(next_block_it->second.ordered_by_size_it);
        // Delete the two blocks
        ++next_block_it;
        m_free_blocks_by_offset_.erase(prev_block_it, next_block_it);
      } else {
        // It is not adjacent to the next block. Another block in between is in use.
        m_free_blocks_by_size_.erase(prev_block_it->second.ordered_by_size_it);
        m_free_blocks_by_offset_.erase(prev_block_it);
      }
    } else if (next_block_it != m_free_blocks_by_offset_.end() && offset + size == next_block_it->first) {
      // It is not adjacent to prev block, but is adjacent to the next block
      new_size = size + next_block_it->second.size;
      new_offset = offset;
      m_free_blocks_by_size_.erase(next_block_it->second.ordered_by_size_it);
      m_free_blocks_by_offset_.erase(next_block_it);
    } else {
      // It is adjacent to nothing
      new_size = size;
      new_offset = offset;
    }
    add_new_block(new_offset, new_size);
    // We add size here, and not new_size, because new_size includes already 
    // freed memory that was merged.
    free_size_ += size;
}

// --- Read/write interface ---
void CXLMemDevice::write_data(uint64_t offset_in_mmap, const void* data, uint32_t write_size) {
  if (status_ != CXL_IPC_STATUS_OK || mmap_addr_ == nullptr) {
    throw std::runtime_error("CXLMemDevice is not ready for write operations");
  }
  if (offset_in_mmap + write_size > size_) {
    throw std::out_of_range("CXLMemDevice write out of bounds: " +
                            std::to_string(offset_in_mmap + write_size) +
                            " > " + std::to_string(size_));
  }
  std::memcpy(mmap_addr_ + offset_in_mmap, data, write_size);
}

void CXLMemDevice::read_data(uint64_t offset_in_mmap, void* data, uint32_t read_size) {
  if (status_ != CXL_IPC_STATUS_OK || mmap_addr_ == nullptr) {
    throw std::runtime_error("CXLMemDevice is not ready for read operations");
  }
  if (offset_in_mmap + read_size > size_) {
    throw std::out_of_range("CXLMemDevice read out of bounds: " +
                            std::to_string(offset_in_mmap + read_size) +
                            " > " + std::to_string(size_));
  }
  std::memcpy(data, mmap_addr_ + offset_in_mmap, read_size);
}

void CXLMemDevice::mark_unhealthy() {
  status_ = CXL_IPC_STATUS_ERROR_GENERIC;
}

void CXLMemDevice::zero_memory_region(uint64_t offset, uint32_t size) {
  if (status_ != CXL_IPC_STATUS_OK || mmap_addr_ == nullptr) {
    throw std::runtime_error("CXLMemDevice is not ready for zeroing memory");
  }
  if (offset + size > size_) {
    throw std::out_of_range("CXLMemDevice zero out of bounds: " +
                            std::to_string(offset + size) +
                            " > " + std::to_string(size_));
  }
  std::memset(mmap_addr_ + offset, 0, size);
}




// --- Constructors ---
// In contrast to the original C impl, no need for separate init and close
// Just do stuff and fail in constructor
// The FM might not want to close if a memdevice fails in the future, possibly
// because we might want to support adding more memdevices
// and cos that would be transparent to the QEMU VMs anyways
CXLMemDevice::CXLMemDevice(std::string path, uint64_t size)
  : free_size_(size),
    path_(std::move(path)),
    size_(size) {
  CXL_MEMDEV_LOG("Initializing Mem Device at " + path_);
  if (path_.empty()) {
    CXL_MEMDEV_LOG("CXLMemDevice path is empty");
    throw std::invalid_argument("CXLMemDevice path cannot be empty");
  }

  fd_ = open(path_.c_str(), O_RDWR);
  if (fd_ < 0) {
    CXL_MEMDEV_LOG("Failed to open CXLMemDevice: " + std::string(strerror(errno)));
    throw std::system_error(errno, std::generic_category(), "Failed to open CXLMemDevice");
  }

  struct stat sb;
  if (::fstat(fd_, &sb) == -1) {
    CXL_MEMDEV_LOG("Failed to fstat CXLMemDevice: " + std::string(strerror(errno)));
    ::close(fd_);
    fd_ = -1;
    throw std::system_error(errno, std::generic_category(), "Failed to fstat CXLMemDevice");
  }

  if (static_cast<uint64_t>(sb.st_size) < size_) {
    CXL_MEMDEV_LOG("CXLMemDevice size is smaller than expected: Got " + std::to_string(sb.st_size) + ", expected " + std::to_string(size_));
    ::close(fd_);
    fd_ = -1;
    throw std::runtime_error("CXLMemDevice size is smaller than expected");
  }

  mmap_addr_ = static_cast<uint8_t*>(::mmap(NULL, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0));
  if (mmap_addr_ == MAP_FAILED) {
    CXL_MEMDEV_LOG("Failed to mmap CXLMemDevice: " + std::string(strerror(errno)));
    ::close(fd_);
    fd_ = -1;
    throw std::system_error(errno, std::generic_category(), "Failed to mmap CXLMemDevice");
  }
  status_ = CXL_IPC_STATUS_OK;
  CXL_MEMDEV_LOG("Successfully initialized Mem Device at " + path_);

  // Initialize mem bookkeeping with single maximum size block
  add_new_block(0, size_);
}

CXLMemDevice::~CXLMemDevice() {
  // I am actually not sure if there is a point in these guard checks
  // since the constructor must have succeeded (in contrast to C), 
  // but just for convention i guess
  if (mmap_addr_ && mmap_addr_ != MAP_FAILED) {
    if (::munmap(mmap_addr_, size_) == -1) {
      CXL_MEMDEV_LOG("Failed to unmap CXLMemDevice: " + std::string(strerror(errno)));
    }
    mmap_addr_ = nullptr;
    CXL_MEMDEV_LOG("CXLMemDevice at " + path_ + " unmapped.");
  }
  if (fd_ >= 0) {
    if (::close(fd_) == -1) {
      CXL_MEMDEV_LOG("Failed to close CXLMemDevice: " + std::string(strerror(errno)));
    }
    fd_ = -1;
    CXL_MEMDEV_LOG("CXLMemDevice at " + path_ + " closed.");
  }
  CXL_MEMDEV_LOG("CXLMemDevice at " + path_ + " destructed.");
}

CXLMemDevice::CXLMemDevice(CXLMemDevice&& other) noexcept 
  : free_size_(std::exchange(other.free_size_, 0)),
    m_free_blocks_by_offset_(std::move(other.m_free_blocks_by_offset_)),
    m_free_blocks_by_size_(std::move(other.m_free_blocks_by_size_)),
    path_(std::move(other.path_)),
    fd_(std::exchange(other.fd_, -1)),
    mmap_addr_(std::exchange(other.mmap_addr_, nullptr)),
    size_(std::exchange(other.size_, 0)),
    status_(std::exchange(other.status_, CXL_IPC_STATUS_ERROR_GENERIC))
{
  CXL_MEMDEV_LOG("Move constructor called for MemDevice at " + path_);
}
}