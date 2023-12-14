#!/usr/bin/env python3
#
# Copyright (c) 2017-2019 Tony Su
# Copyright (c) 2023 Red Hat, Inc.
#
# SPDX-License-Identifier: MIT
#
# Adapted from https://github.com/peitaosu/XML-Preprocessor
#
"""This is a XML Preprocessor which can be used to process your XML file before
you use it, to process conditional statements, variables, iteration
statements, error/warning, execute command, etc.

## XML Schema

### Include Files
```
<?include path/to/file ?>
```

### Variables
```
$(env.EnvironmentVariable)

$(sys.SystemVariable)

$(var.CustomVariable)
```

### Conditional Statements
```
<?if ?>

<?ifdef ?>

<?ifndef ?>

<?else?>

<?elseif ?>

<?endif?>
```

### Iteration Statements
```
<?foreach VARNAME in 1;2;3?>
    $(var.VARNAME)
<?endforeach?>
```

### Errors and Warnings
```
<?error "This is error message!" ?>

<?warning "This is warning message!" ?>
```

### Commands
```
<? cmd "echo hello world" ?>
```
"""

import os
import platform
import re
import subprocess
import sys
from typing import Optional
from xml.dom import minidom


class Preprocessor():
    """This class holds the XML preprocessing state"""

    def __init__(self):
        self.sys_vars = {
            "ARCH": platform.architecture()[0],
            "SOURCE": os.path.abspath(__file__),
            "CURRENT": os.getcwd(),
        }
        self.cus_vars = {}

    def _pp_include(self, xml_str: str) -> str:
        include_regex = r"(<\?include([\w\s\\/.:_-]+)\s*\?>)"
        matches = re.findall(include_regex, xml_str)
        for group_inc, group_xml in matches:
            inc_file_path = group_xml.strip()
            with open(inc_file_path, "r", encoding="utf-8") as inc_file:
                inc_file_content = inc_file.read()
                xml_str = xml_str.replace(group_inc, inc_file_content)
        return xml_str

    def _pp_env_var(self, xml_str: str) -> str:
        envvar_regex = r"(\$\(env\.(\w+)\))"
        matches = re.findall(envvar_regex, xml_str)
        for group_env, group_var in matches:
            xml_str = xml_str.replace(group_env, os.environ[group_var])
        return xml_str

    def _pp_sys_var(self, xml_str: str) -> str:
        sysvar_regex = r"(\$\(sys\.(\w+)\))"
        matches = re.findall(sysvar_regex, xml_str)
        for group_sys, group_var in matches:
            xml_str = xml_str.replace(group_sys, self.sys_vars[group_var])
        return xml_str

    def _pp_cus_var(self, xml_str: str) -> str:
        define_regex = r"(<\?define\s*(\w+)\s*=\s*([\w\s\"]+)\s*\?>)"
        matches = re.findall(define_regex, xml_str)
        for group_def, group_name, group_var in matches:
            group_name = group_name.strip()
            group_var = group_var.strip().strip("\"")
            self.cus_vars[group_name] = group_var
            xml_str = xml_str.replace(group_def, "")
        cusvar_regex = r"(\$\(var\.(\w+)\))"
        matches = re.findall(cusvar_regex, xml_str)
        for group_cus, group_var in matches:
            xml_str = xml_str.replace(
                group_cus,
                self.cus_vars.get(group_var, "")
            )
        return xml_str

    def _pp_foreach(self, xml_str: str) -> str:
        foreach_regex = r"(<\?foreach\s+(\w+)\s+in\s+([\w;]+)\s*\?>(.*)<\?endforeach\?>)"
        matches = re.findall(foreach_regex, xml_str)
        for group_for, group_name, group_vars, group_text in matches:
            group_texts = ""
            for var in group_vars.split(";"):
                self.cus_vars[group_name] = var
                group_texts += self._pp_cus_var(group_text)
            xml_str = xml_str.replace(group_for, group_texts)
        return xml_str

    def _pp_error_warning(self, xml_str: str) -> str:
        error_regex = r"<\?error\s*\"([^\"]+)\"\s*\?>"
        matches = re.findall(error_regex, xml_str)
        for group_var in matches:
            raise RuntimeError("[Error]: " + group_var)
        warning_regex = r"(<\?warning\s*\"([^\"]+)\"\s*\?>)"
        matches = re.findall(warning_regex, xml_str)
        for group_wrn, group_var in matches:
            print("[Warning]: " + group_var)
            xml_str = xml_str.replace(group_wrn, "")
        return xml_str

    def _pp_if_eval(self, xml_str: str) -> str:
        ifelif_regex = (
            r"(<\?(if|elseif)\s*([^\"\s=<>!]+)\s*([!=<>]+)\s*\"*([^\"=<>!]+)\"*\s*\?>)"
        )
        matches = re.findall(ifelif_regex, xml_str)
        for ifelif, tag, left, operator, right in matches:
            if "<" in operator or ">" in operator:
                result = eval(f"{left} {operator} {right}")
            else:
                result = eval(f'"{left}" {operator} "{right}"')
            xml_str = xml_str.replace(ifelif, f"<?{tag} {result}?>")
        return xml_str

    def _pp_ifdef_ifndef(self, xml_str: str) -> str:
        ifndef_regex = r"(<\?(ifdef|ifndef)\s*([\w]+)\s*\?>)"
        matches = re.findall(ifndef_regex, xml_str)
        for group_ifndef, group_tag, group_var in matches:
            if group_tag == "ifdef":
                result = group_var in self.cus_vars
            else:
                result = group_var not in self.cus_vars
            xml_str = xml_str.replace(group_ifndef, f"<?if {result}?>")
        return xml_str

    def _pp_if_elseif(self, xml_str: str) -> str:
        if_elif_else_regex = (
            r"(<\?if\s(True|False)\?>"
            r"(.*?)"
            r"<\?elseif\s(True|False)\?>"
            r"(.*?)"
            r"<\?else\?>"
            r"(.*?)"
            r"<\?endif\?>)"
        )
        if_else_regex = (
            r"(<\?if\s(True|False)\?>"
            r"(.*?)"
            r"<\?else\?>"
            r"(.*?)"
            r"<\?endif\?>)"
        )
        if_regex = r"(<\?if\s(True|False)\?>(.*?)<\?endif\?>)"
        matches = re.findall(if_elif_else_regex, xml_str, re.DOTALL)
        for (group_full, group_if, group_if_elif, group_elif,
             group_elif_else, group_else) in matches:
            result = ""
            if group_if == "True":
                result = group_if_elif
            elif group_elif == "True":
                result = group_elif_else
            else:
                result = group_else
            xml_str = xml_str.replace(group_full, result)
        matches = re.findall(if_else_regex, xml_str, re.DOTALL)
        for group_full, group_if, group_if_else, group_else in matches:
            result = ""
            if group_if == "True":
                result = group_if_else
            else:
                result = group_else
            xml_str = xml_str.replace(group_full, result)
        matches = re.findall(if_regex, xml_str, re.DOTALL)
        for group_full, group_if, group_text in matches:
            result = ""
            if group_if == "True":
                result = group_text
            xml_str = xml_str.replace(group_full, result)
        return xml_str

    def _pp_command(self, xml_str: str) -> str:
        cmd_regex = r"(<\?cmd\s*\"([^\"]+)\"\s*\?>)"
        matches = re.findall(cmd_regex, xml_str)
        for group_cmd, group_exec in matches:
            output = subprocess.check_output(
                group_exec, shell=True,
                text=True, stderr=subprocess.STDOUT
            )
            xml_str = xml_str.replace(group_cmd, output)
        return xml_str

    def _pp_blanks(self, xml_str: str) -> str:
        right_blank_regex = r">[\n\s\t\r]*"
        left_blank_regex = r"[\n\s\t\r]*<"
        xml_str = re.sub(right_blank_regex, ">", xml_str)
        xml_str = re.sub(left_blank_regex, "<", xml_str)
        return xml_str

    def preprocess(self, xml_str: str) -> str:
        fns = [
            self._pp_blanks,
            self._pp_include,
            self._pp_foreach,
            self._pp_env_var,
            self._pp_sys_var,
            self._pp_cus_var,
            self._pp_if_eval,
            self._pp_ifdef_ifndef,
            self._pp_if_elseif,
            self._pp_command,
            self._pp_error_warning,
        ]

        while True:
            changed = False
            for func in fns:
                out_xml = func(xml_str)
                if not changed and out_xml != xml_str:
                    changed = True
                xml_str = out_xml
            if not changed:
                break

        return xml_str


def preprocess_xml(path: str) -> str:
    with open(path, "r", encoding="utf-8") as original_file:
        input_xml = original_file.read()

        proc = Preprocessor()
        return proc.preprocess(input_xml)


def save_xml(xml_str: str, path: Optional[str]):
    xml = minidom.parseString(xml_str)
    with open(path, "w", encoding="utf-8") if path else sys.stdout as output_file:
        output_file.write(xml.toprettyxml())


def main():
    if len(sys.argv) < 2:
        print("Usage: xml-preprocessor input.xml [output.xml]")
        sys.exit(1)

    output_file = None
    if len(sys.argv) == 3:
        output_file = sys.argv[2]

    input_file = sys.argv[1]
    output_xml = preprocess_xml(input_file)
    save_xml(output_xml, output_file)


if __name__ == "__main__":
    main()
