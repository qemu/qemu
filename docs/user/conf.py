# -*- coding: utf-8 -*-
#
# QEMU documentation build configuration file for the 'user' manual.
#
# This includes the top level conf file and then makes any necessary tweaks.
import sys
import os

qemu_docdir = os.path.abspath("..")
parent_config = os.path.join(qemu_docdir, "conf.py")
exec(compile(open(parent_config, "rb").read(), parent_config, 'exec'))

# This slightly misuses the 'description', but is the best way to get
# the manual title to appear in the sidebar.
html_theme_options['description'] = u'User Mode Emulation User''s Guide'
