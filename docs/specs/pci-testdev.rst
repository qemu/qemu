====================
QEMU PCI test device
====================

``pci-testdev`` is a device used for testing low level IO.

The device implements up to three BARs: BAR0, BAR1 and BAR2.
Each of BAR 0+1 can be memory or IO. Guests must detect
BAR types and act accordingly.

BAR 0+1 size is up to 4K bytes each.
BAR 0+1 starts with the following header:

.. code-block:: c

  typedef struct PCITestDevHdr {
      uint8_t test;        /* write-only, starts a given test number */
      uint8_t width_type;  /*
                            * read-only, type and width of access for a given test.
                            * 1,2,4 for byte,word or long write.
                            * any other value if test not supported on this BAR
                            */
      uint8_t pad0[2];
      uint32_t offset;     /* read-only, offset in this BAR for a given test */
      uint32_t data;       /* read-only, data to use for a given test */
      uint32_t count;      /* for debugging. number of writes detected. */
      uint8_t name[];      /* for debugging. 0-terminated ASCII string. */
  } PCITestDevHdr;

All registers are little endian.

The device is expected to always implement tests 0 to N on each BAR, and to add new
tests with higher numbers.  In this way a guest can scan test numbers until it
detects an access type that it does not support on this BAR, then stop.

BAR2 is a 64bit memory BAR, without backing storage.  It is disabled
by default and can be enabled using the ``membar=<size>`` property.  This
can be used to test whether guests handle PCI BARs of a specific
(possibly quite large) size correctly.
