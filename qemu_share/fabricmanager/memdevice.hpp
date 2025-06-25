#ifndef CXL_MEM_DEVICE_HPP
#define CXL_MEM_DEVICE_HPP

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include "../includes/cxl_switch_ipc.h"

namespace cxl_fm {
/**
  Represents a CXL Memory Device. At the moment, it is really just a 
  host-backed memory file.

  This class manages its own memory allocations for RPC connections.
    The class uses a variable-length memory allocation strategy.
    Reference: https://www.codeproject.com/Articles/1180070/Simple-Variable-Size-Memory-Block-Allocator

    Need not be so complicated, and the current design probably suffices.
    This implementation provides logarithmic allocation/free/merge.
  
  The Fabric Manager requests XYZ amount of memory from the memory device.
  If the memory device can service this request, it returns an offset + length
  to the Fabric Manager which is struct AllocatedRegionInfo.

  At the RPC client/server end, they have a logical 0 view and their 
  stores/loads are intercepted by the QEMU Object and routed to the
  server/Fabric Manager. The FM would route the loads/store to the
  appropriate offset.

*/
class CXLMemDevice {
  
/**
  --- Memory management ---

  For the public memory management, only the allocate() is exposed
  which returns a std::optional
  containing the offset and size of the allocated block if successful.
  On the failure, the FM should request another block.

*/ 
private:
  // Total free size in the memory device
  size_t free_size_;
  // Forward declaration
  struct FreeBlockInfo;
  // Memory blocks are sorted by their offsets
  using TFreeBlocksByOffsetMap = std::map<size_t, FreeBlockInfo, std::less<size_t>>;
  // Memory blocks sorted by their size
  using TFreeBlocksBySizeMap = std::multimap<size_t, TFreeBlocksByOffsetMap::iterator, std::less<size_t>>;

  typedef struct FreeBlockInfo {
    // Block size
    size_t size;
    // Iterator referencing this block in the multimap sorted by block size
    TFreeBlocksBySizeMap::iterator ordered_by_size_it;

    FreeBlockInfo(size_t _size): size(_size) {}
  } FreeBlockInfo;

  TFreeBlocksByOffsetMap m_free_blocks_by_offset_;
  TFreeBlocksBySizeMap m_free_blocks_by_size_;
  // Helper function to update the two maps when a new block is added
  // This is also called by the init() function when the size has been determined
  void add_new_block(size_t offset, size_t size);
public:
  // Returns an offset of the requested size if successful.
  std::optional<size_t> allocate(size_t size);
  // Called when the RPC connection should be freed up
  // size can be provided here, as the FM should also be responsible for tracking
  // how much size an RPC connection required from the mem device
  void free(size_t offset, size_t size);

/**
  --- CXL Memory Device ---

  Boiler plate safe handling of resources done here.
  Interface for reading and writing.

*/
public:
  CXLMemDevice() = default;
  CXLMemDevice(std::string path, uint64_t size);
  ~CXLMemDevice();

  CXLMemDevice(CXLMemDevice&& other) noexcept;

  CXLMemDevice(const CXLMemDevice&) = delete;
  CXLMemDevice& operator=(const CXLMemDevice&) = delete;


  // Read/write public interface called by FM
  void write_data(uint64_t offset_in_mmap, const void* data, uint32_t write_size);
  void read_data(uint64_t offset_in_mmap, void* data, uint32_t read_size);
  // Hack admin method to mark dead
  void mark_unhealthy();

private:
  std::string path_;
  int fd_ = -1;
  uint8_t *mmap_addr_ = nullptr;
  uint64_t size_ = 0;
  cxl_ipc_status_t status_ = CXL_IPC_STATUS_OK;
private:
  void zero_memory_region(uint64_t offset, uint32_t size);

};

}
#endif