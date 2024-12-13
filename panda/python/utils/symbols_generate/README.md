# Symbols Generate

The goal of this script is to generate a header that pycparser can parse so that
cffi can understand the types from PANDA. 

This script is both amazing and awful.

It works like this:
- use the C preprocessor to generate a header file containing all our
types
- filter out lines that start with '#'
- run [tree-sitter](https://tree-sitter.github.io/) on the header to find places to fix up
- use gdb to resolve complex statements