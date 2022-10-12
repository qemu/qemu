"""
Extras are PyPANDA plugins which you can import into other python analyses. Typically
this is done by passing a handle from your script's PANDA object to the plugin.
"""

# Note file names should not contain underscores, let's keep these in lower
# camelCase going forward (e.g., modeFilter) so they match the class names.
from .fileFaker import FakeFile, FileFaker
from .fileHook import FileHook
from .ioctlFaker import IoctlFaker
from .modeFilter import ModeFilter
from .procWriteCapture import ProcWriteCapture
from .procTrace import ProcGraph


__all__ = ['FakeFile', 'FileFaker', 'FileHook', 'IoctlFaker', 'ModeFilter', 'ProcWriteCapture',
           'Snake', 'ProcGraph']
