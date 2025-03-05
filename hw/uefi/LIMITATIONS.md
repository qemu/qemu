known issues and limitations
----------------------------

* works only on little endian hosts
  - accessing structs in guest ram is done without endian conversion.
* works only for 64-bit guests
  - UINTN is mapped to uint64_t, for 32-bit guests that would be uint32_t
