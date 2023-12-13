.. _sec_005finvocation:

Invocation
----------

.. parsed-literal::

   |qemu_system| [options] [disk_image]

disk_image is a raw hard disk image for IDE hard disk 0. Some targets do
not need a disk image.

When dealing with options parameters as arbitrary strings containing
commas, such as in "file=my,file" and "string=a,b", it's necessary to
double the commas. For instance,"-fw_cfg name=z,string=a,,b" will be
parsed as "-fw_cfg name=z,string=a,b".

.. hxtool-doc:: qemu-options.hx

Device URL Syntax
~~~~~~~~~~~~~~~~~

.. include:: device-url-syntax.rst.inc
