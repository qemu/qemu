..
   Generally useful rST substitution definitions. This is included for
   all rST files as part of the epilogue by docs/conf.py.  conf.py
   also defines some dynamically generated substitutions like CONFDIR.

   Note that |qemu_system| is intended to be used inside a parsed-literal
   block: the definition must not include extra literal formatting with
   ``..``: this works in the HTML output but the manpages will end up
   misrendered with following normal text incorrectly in boldface.

.. |qemu_system| replace:: qemu-system-x86_64
