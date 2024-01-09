==============
Best practices
==============

Debugging
=========

The migration stream can be analyzed thanks to ``scripts/analyze-migration.py``.

Example usage:

.. code-block:: shell

  $ qemu-system-x86_64 -display none -monitor stdio
  (qemu) migrate "exec:cat > mig"
  (qemu) q
  $ ./scripts/analyze-migration.py -f mig
  {
    "ram (3)": {
        "section sizes": {
            "pc.ram": "0x0000000008000000",
  ...

See also ``analyze-migration.py -h`` help for more options.

Firmware
========

Migration migrates the copies of RAM and ROM, and thus when running
on the destination it includes the firmware from the source. Even after
resetting a VM, the old firmware is used.  Only once QEMU has been restarted
is the new firmware in use.

- Changes in firmware size can cause changes in the required RAMBlock size
  to hold the firmware and thus migration can fail.  In practice it's best
  to pad firmware images to convenient powers of 2 with plenty of space
  for growth.

- Care should be taken with device emulation code so that newer
  emulation code can work with older firmware to allow forward migration.

- Care should be taken with newer firmware so that backward migration
  to older systems with older device emulation code will work.

In some cases it may be best to tie specific firmware versions to specific
versioned machine types to cut down on the combinations that will need
support.  This is also useful when newer versions of firmware outgrow
the padding.
