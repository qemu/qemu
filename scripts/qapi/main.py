# This work is licensed under the terms of the GNU GPL, version 2 or later.
# See the COPYING file in the top-level directory.

"""
QAPI Generator

This is the main entry point for generating C code from the QAPI schema.
"""

import argparse
from importlib import import_module
import sys
from typing import Optional

from .backend import QAPIBackend, QAPICBackend
from .common import must_match
from .error import QAPIError
from .schema import QAPISchema


def invalid_prefix_char(prefix: str) -> Optional[str]:
    match = must_match(r'([A-Za-z_.-][A-Za-z0-9_.-]*)?', prefix)
    if match.end() != len(prefix):
        return prefix[match.end()]
    return None


def create_backend(path: str) -> QAPIBackend:
    if path is None:
        return QAPICBackend()

    module_path, dot, class_name = path.rpartition('.')
    if not dot:
        raise QAPIError("argument of -B must be of the form MODULE.CLASS")

    try:
        mod = import_module(module_path)
    except Exception as ex:
        raise QAPIError(f"unable to import '{module_path}': {ex}") from ex

    try:
        klass = getattr(mod, class_name)
    except AttributeError as ex:
        raise QAPIError(
            f"module '{module_path}' has no class '{class_name}'") from ex

    try:
        backend = klass()
    except Exception as ex:
        raise QAPIError(
            f"backend '{path}' cannot be instantiated: {ex}") from ex

    if not isinstance(backend, QAPIBackend):
        raise QAPIError(
            f"backend '{path}' must be an instance of QAPIBackend")

    return backend


def main() -> int:
    """
    gapi-gen executable entry point.
    Expects arguments via sys.argv, see --help for details.

    :return: int, 0 on success, 1 on failure.
    """
    parser = argparse.ArgumentParser(
        description='Generate code from a QAPI schema')
    parser.add_argument('-b', '--builtins', action='store_true',
                        help="generate code for built-in types")
    parser.add_argument('-o', '--output-dir', action='store',
                        default='',
                        help="write output to directory OUTPUT_DIR")
    parser.add_argument('-p', '--prefix', action='store',
                        default='',
                        help="prefix for symbols")
    parser.add_argument('-u', '--unmask-non-abi-names', action='store_true',
                        dest='unmask',
                        help="expose non-ABI names in introspection")
    parser.add_argument('-B', '--backend', default=None,
                        help="Python module name for code generator")

    # Option --suppress-tracing exists so we can avoid solving build system
    # problems.  TODO Drop it when we no longer need it.
    parser.add_argument('--suppress-tracing', action='store_true',
                        help="suppress adding trace events to qmp marshals")

    parser.add_argument('schema', action='store')
    args = parser.parse_args()

    funny_char = invalid_prefix_char(args.prefix)
    if funny_char:
        msg = f"funny character '{funny_char}' in argument of --prefix"
        print(f"{sys.argv[0]}: {msg}", file=sys.stderr)
        return 1

    try:
        schema = QAPISchema(args.schema)
        backend = create_backend(args.backend)
        backend.generate(schema,
                         output_dir=args.output_dir,
                         prefix=args.prefix,
                         unmask=args.unmask,
                         builtins=args.builtins,
                         gen_tracing=not args.suppress_tracing)
    except QAPIError as err:
        print(err, file=sys.stderr)
        return 1
    return 0
