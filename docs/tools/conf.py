# -*- coding: utf-8 -*-
#
# QEMU documentation build configuration file for the 'tools' manual.
#
# This includes the top level conf file and then makes any necessary tweaks.
import sys
import os

qemu_docdir = os.path.abspath("..")
parent_config = os.path.join(qemu_docdir, "conf.py")
exec(compile(open(parent_config, "rb").read(), parent_config, 'exec'))

# This slightly misuses the 'description', but is the best way to get
# the manual title to appear in the sidebar.
html_theme_options['description'] = \
    u'Tools Guide'

# One entry per manual page. List of tuples
# (source start file, name, description, authors, manual section).
man_pages = [
    ('qemu-img', 'qemu-img', u'QEMU disk image utility',
     ['Fabrice Bellard'], 1),
    ('qemu-nbd', 'qemu-nbd', u'QEMU Disk Network Block Device Server',
     ['Anthony Liguori <anthony@codemonkey.ws>'], 8),
    ('qemu-trace-stap', 'qemu-trace-stap', u'QEMU SystemTap trace tool',
     [], 1),
    ('virtfs-proxy-helper', 'virtfs-proxy-helper',
     u'QEMU 9p virtfs proxy filesystem helper',
     ['M. Mohan Kumar'], 1),
    ('virtiofsd', 'virtiofsd', u'QEMU virtio-fs shared file system daemon',
     ['Stefan Hajnoczi <stefanha@redhat.com>',
      'Masayoshi Mizuma <m.mizuma@jp.fujitsu.com>'], 1),
]
