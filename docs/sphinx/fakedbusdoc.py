# D-Bus XML documentation extension, compatibility gunk for <sphinx4
#
# Copyright (C) 2021, Red Hat Inc.
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Author: Marc-André Lureau <marcandre.lureau@redhat.com>
"""dbus-doc is a Sphinx extension that provides documentation from D-Bus XML."""

from docutils.parsers.rst import Directive
from sphinx.application import Sphinx
from typing import Any, Dict


class FakeDBusDocDirective(Directive):
    has_content = True
    required_arguments = 1

    def run(self):
        return []


def setup(app: Sphinx) -> Dict[str, Any]:
    """Register a fake dbus-doc directive with Sphinx"""
    app.add_directive("dbus-doc", FakeDBusDocDirective)
