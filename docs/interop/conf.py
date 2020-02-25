# -*- coding: utf-8 -*-
#
# QEMU documentation build configuration file for the 'interop' manual.
#
# This includes the top level conf file and then makes any necessary tweaks.
import sys
import os

qemu_docdir = os.path.abspath("..")
parent_config = os.path.join(qemu_docdir, "conf.py")
exec(compile(open(parent_config, "rb").read(), parent_config, 'exec'))

# This slightly misuses the 'description', but is the best way to get
# the manual title to appear in the sidebar.
html_theme_options['description'] = u'System Emulation Management and Interoperability Guide'

# One entry per manual page. List of tuples
# (source start file, name, description, authors, manual section).
man_pages = [
    ('qemu-ga', 'qemu-ga', u'QEMU Guest Agent',
     ['Michael Roth <mdroth@linux.vnet.ibm.com>'], 8),
]
