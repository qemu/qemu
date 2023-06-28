#!/usr/bin/env python3
#
# Copyright (c) 2023 Red Hat, Inc.
#
# SPDX-License-Identifier: MIT
"""Unit tests for xml-preprocess"""

import contextlib
import importlib
import os
import platform
import subprocess
import tempfile
import unittest
from io import StringIO

xmlpp = importlib.import_module("xml-preprocess")


class TestXmlPreprocess(unittest.TestCase):
    """Tests for xml-preprocess.Preprocessor"""

    def test_preprocess_xml(self):
        with tempfile.NamedTemporaryFile(mode="w", delete=False) as temp_file:
            temp_file.write("<root></root>")
            temp_file_name = temp_file.name
        result = xmlpp.preprocess_xml(temp_file_name)
        self.assertEqual(result, "<root></root>")
        os.remove(temp_file_name)

    def test_save_xml(self):
        with tempfile.NamedTemporaryFile(mode="w", delete=False) as temp_file:
            temp_file_name = temp_file.name
            xmlpp.save_xml("<root></root>", temp_file_name)
        self.assertTrue(os.path.isfile(temp_file_name))
        os.remove(temp_file_name)

    def test_include(self):
        with tempfile.NamedTemporaryFile(mode="w", delete=False) as inc_file:
            inc_file.write("<included>Content from included file</included>")
            inc_file_name = inc_file.name
        xml_str = f"<?include {inc_file_name} ?>"
        expected = "<included>Content from included file</included>"
        xpp = xmlpp.Preprocessor()
        result = xpp.preprocess(xml_str)
        self.assertEqual(result, expected)
        os.remove(inc_file_name)
        self.assertRaises(FileNotFoundError, xpp.preprocess, xml_str)

    def test_envvar(self):
        os.environ["TEST_ENV_VAR"] = "TestValue"
        xml_str = "<root>$(env.TEST_ENV_VAR)</root>"
        expected = "<root>TestValue</root>"
        xpp = xmlpp.Preprocessor()
        result = xpp.preprocess(xml_str)
        self.assertEqual(result, expected)
        self.assertRaises(KeyError, xpp.preprocess, "$(env.UNKNOWN)")

    def test_sys_var(self):
        xml_str = "<root>$(sys.ARCH)</root>"
        expected = f"<root>{platform.architecture()[0]}</root>"
        xpp = xmlpp.Preprocessor()
        result = xpp.preprocess(xml_str)
        self.assertEqual(result, expected)
        self.assertRaises(KeyError, xpp.preprocess, "$(sys.UNKNOWN)")

    def test_cus_var(self):
        xml_str = "<root>$(var.USER)</root>"
        expected = "<root></root>"
        xpp = xmlpp.Preprocessor()
        result = xpp.preprocess(xml_str)
        self.assertEqual(result, expected)
        xml_str = "<?define USER=FOO?><root>$(var.USER)</root>"
        expected = "<root>FOO</root>"
        xpp = xmlpp.Preprocessor()
        result = xpp.preprocess(xml_str)
        self.assertEqual(result, expected)

    def test_error_warning(self):
        xml_str = "<root><?warning \"test warn\"?></root>"
        expected = "<root></root>"
        xpp = xmlpp.Preprocessor()
        out = StringIO()
        with contextlib.redirect_stdout(out):
            result = xpp.preprocess(xml_str)
        self.assertEqual(result, expected)
        self.assertEqual(out.getvalue(), "[Warning]: test warn\n")
        self.assertRaises(RuntimeError, xpp.preprocess, "<?error \"test\"?>")

    def test_cmd(self):
        xpp = xmlpp.Preprocessor()
        result = xpp.preprocess('<root><?cmd "echo hello world"?></root>')
        self.assertEqual(result, "<root>hello world</root>")
        self.assertRaises(
            subprocess.CalledProcessError,
            xpp.preprocess, '<?cmd "test-unknown-cmd"?>'
        )

    def test_foreach(self):
        xpp = xmlpp.Preprocessor()
        result = xpp.preprocess(
            '<root><?foreach x in a;b;c?>$(var.x)<?endforeach?></root>'
        )
        self.assertEqual(result, "<root>abc</root>")

    def test_if_elseif(self):
        xpp = xmlpp.Preprocessor()
        result = xpp.preprocess('<root><?if True?>ok<?endif?></root>')
        self.assertEqual(result, "<root>ok</root>")
        result = xpp.preprocess('<root><?if False?>ok<?endif?></root>')
        self.assertEqual(result, "<root></root>")
        result = xpp.preprocess('<root><?if True?>ok<?else?>ko<?endif?></root>')
        self.assertEqual(result, "<root>ok</root>")
        result = xpp.preprocess('<root><?if False?>ok<?else?>ko<?endif?></root>')
        self.assertEqual(result, "<root>ko</root>")
        result = xpp.preprocess(
            '<root><?if False?>ok<?elseif True?>ok2<?else?>ko<?endif?></root>'
        )
        self.assertEqual(result, "<root>ok2</root>")
        result = xpp.preprocess(
            '<root><?if False?>ok<?elseif False?>ok<?else?>ko<?endif?></root>'
        )
        self.assertEqual(result, "<root>ko</root>")

    def test_ifdef(self):
        xpp = xmlpp.Preprocessor()
        result = xpp.preprocess('<root><?ifdef USER?>ok<?else?>ko<?endif?></root>')
        self.assertEqual(result, "<root>ko</root>")
        result = xpp.preprocess(
            '<?define USER=FOO?><root><?ifdef USER?>ok<?else?>ko<?endif?></root>'
        )
        self.assertEqual(result, "<root>ok</root>")


if __name__ == "__main__":
    unittest.main()
