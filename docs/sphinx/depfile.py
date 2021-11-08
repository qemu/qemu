# coding=utf-8
#
# QEMU depfile generation extension
#
# Copyright (c) 2020 Red Hat, Inc.
#
# This work is licensed under the terms of the GNU GPLv2 or later.
# See the COPYING file in the top-level directory.

"""depfile is a Sphinx extension that writes a dependency file for
   an external build system"""

import os
import sphinx
import sys
from pathlib import Path

__version__ = '1.0'

def get_infiles(env):
    for x in env.found_docs:
        yield env.doc2path(x)
        yield from ((os.path.join(env.srcdir, dep)
                    for dep in env.dependencies[x]))
    for mod in sys.modules.values():
        if hasattr(mod, '__file__'):
            if mod.__file__:
                yield mod.__file__
    # this is perhaps going to include unused files:
    for static_path in env.config.html_static_path + env.config.templates_path:
        for path in Path(static_path).rglob('*'):
            yield str(path)


def write_depfile(app, exception):
    if exception:
        return

    env = app.env
    if not env.config.depfile:
        return

    # Using a directory as the output file does not work great because
    # its timestamp does not necessarily change when the contents change.
    # So create a timestamp file.
    if env.config.depfile_stamp:
        with open(env.config.depfile_stamp, 'w') as f:
            pass

    with open(env.config.depfile, 'w') as f:
        print((env.config.depfile_stamp or app.outdir) + ": \\", file=f)
        print(*get_infiles(env), file=f)
        for x in get_infiles(env):
            print(x + ":", file=f)


def setup(app):
    app.add_config_value('depfile', None, 'env')
    app.add_config_value('depfile_stamp', None, 'env')
    app.connect('build-finished', write_depfile)

    return dict(
        version = __version__,
        parallel_read_safe = True,
        parallel_write_safe = True
    )
