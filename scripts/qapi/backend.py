# This work is licensed under the terms of the GNU GPL, version 2 or later.
# See the COPYING file in the top-level directory.

from abc import ABC, abstractmethod

from .commands import gen_commands
from .events import gen_events
from .features import gen_features
from .introspect import gen_introspect
from .schema import QAPISchema
from .types import gen_types
from .visit import gen_visit


class QAPIBackend(ABC):
    # pylint: disable=too-few-public-methods

    @abstractmethod
    def generate(self,
                 schema: QAPISchema,
                 output_dir: str,
                 prefix: str,
                 unmask: bool,
                 builtins: bool,
                 gen_tracing: bool) -> None:
        """
        Generate code for the given schema into the target directory.

        :param schema: The primary QAPI schema object.
        :param output_dir: The output directory to store generated code.
        :param prefix: Optional C-code prefix for symbol names.
        :param unmask: Expose non-ABI names through introspection?
        :param builtins: Generate code for built-in types?

        :raise QAPIError: On failures.
        """


class QAPICBackend(QAPIBackend):
    # pylint: disable=too-few-public-methods

    def generate(self,
                 schema: QAPISchema,
                 output_dir: str,
                 prefix: str,
                 unmask: bool,
                 builtins: bool,
                 gen_tracing: bool) -> None:
        """
        Generate C code for the given schema into the target directory.

        :param schema_file: The primary QAPI schema file.
        :param output_dir: The output directory to store generated code.
        :param prefix: Optional C-code prefix for symbol names.
        :param unmask: Expose non-ABI names through introspection?
        :param builtins: Generate code for built-in types?

        :raise QAPIError: On failures.
        """
        gen_types(schema, output_dir, prefix, builtins)
        gen_features(schema, output_dir, prefix)
        gen_visit(schema, output_dir, prefix, builtins)
        gen_commands(schema, output_dir, prefix, gen_tracing)
        gen_events(schema, output_dir, prefix)
        gen_introspect(schema, output_dir, prefix, unmask)
