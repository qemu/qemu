"""
QAPI features generator

Copyright 2024 Red Hat

This work is licensed under the terms of the GNU GPL, version 2.
# See the COPYING file in the top-level directory.
"""

from typing import ValuesView

from .common import c_enum_const, c_name
from .gen import QAPISchemaMonolithicCVisitor
from .schema import QAPISchema, QAPISchemaFeature


class QAPISchemaGenFeatureVisitor(QAPISchemaMonolithicCVisitor):

    def __init__(self, prefix: str):
        super().__init__(
            prefix, 'qapi-features',
            ' * Schema-defined QAPI features',
            __doc__)

        self.features: ValuesView[QAPISchemaFeature]

    def visit_begin(self, schema: QAPISchema) -> None:
        self.features = schema.features()
        self._genh.add("#include \"qapi/util.h\"\n\n")

    def visit_end(self) -> None:
        self._genh.add("typedef enum {\n")
        for f in self.features:
            self._genh.add(f"    {c_enum_const('qapi_feature', f.name)}")
            if f.name in QAPISchemaFeature.SPECIAL_NAMES:
                self._genh.add(f" = {c_enum_const('qapi', f.name)},\n")
            else:
                self._genh.add(",\n")

        self._genh.add("} " + c_name('QapiFeature') + ";\n")


def gen_features(schema: QAPISchema,
                 output_dir: str,
                 prefix: str) -> None:
    vis = QAPISchemaGenFeatureVisitor(prefix)
    schema.visit(vis)
    vis.write(output_dir)
