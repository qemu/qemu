..
   Generally useful rST substitution definitions. This is included for
   all rST files as part of the epilogue by docs/conf.py.  conf.py
   also defines some dynamically generated substitutions like CONFDIR.

   Note that |qemu_system| and |qemu_system_x86| are intended to be
   used inside a parsed-literal block: the definition must not include
   extra literal formatting with ``..``: this works in the HTML output
   but the manpages will end up misrendered with following normal text
   incorrectly in boldface.

.. |qemu_system| replace:: qemu-system-x86_64
.. |qemu_system_x86| replace:: qemu_system-x86_64
.. |I2C| replace:: I\ :sup:`2`\ C
.. |I2S| replace:: I\ :sup:`2`\ S
