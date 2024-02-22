
==================
QEMU Documentation
==================

QEMU's documentation is written in reStructuredText format and
built using the Sphinx documentation generator. We generate both
the HTML manual and the manpages from the some documentation sources.

hxtool and .hx files
--------------------

The documentation for QEMU command line options and Human Monitor Protocol
(HMP) commands is written in files with the ``.hx`` suffix. These
are processed in two ways:

 * ``scripts/hxtool`` creates C header files from them, which are included
   in QEMU to do things like handle the ``--help`` option output
 * a Sphinx extension in ``docs/sphinx/hxtool.py`` generates rST output
   to be included in the HTML or manpage documentation

The syntax of these ``.hx`` files is simple. It is broadly an
alternation of C code put into the C output and rST format text
put into the documentation. A few special directives are recognised;
these are all-caps and must be at the beginning of the line.

``HXCOMM`` is the comment marker. The line, including any arbitrary
text after the marker, is discarded and appears neither in the C output
nor the documentation output.

``SRST`` starts a reStructuredText section. Following lines
are put into the documentation verbatim, and discarded from the C output.
The alternative form ``SRST()`` is used to define a label which can be
referenced from elsewhere in the rST documentation. The label will take
the form ``<DOCNAME-HXFILE-LABEL>``, where ``DOCNAME`` is the name of the
top level rST file, ``HXFILE`` is the filename of the .hx file without
the ``.hx`` extension, and ``LABEL`` is the text provided within the
``SRST()`` directive. For example,
``<system/invocation-qemu-options-initrd>``.

``ERST`` ends the documentation section started with ``SRST``,
and switches back to a C code section.

``DEFHEADING()`` defines a heading that should appear in both the
``--help`` output and in the documentation. This directive should
be in the C code block. If there is a string inside the brackets,
this is the heading to use. If this string is empty, it produces
a blank line in the ``--help`` output and is ignored for the rST
output.

``ARCHHEADING()`` is a variant of ``DEFHEADING()`` which produces
the heading only if the specified guest architecture was compiled
into QEMU. This should be avoided in new documentation.

Within C code sections, you should check the comments at the top
of the file to see what the expected usage is, because this
varies between files. For instance in ``qemu-options.hx`` we use
the ``DEF()`` macro to define each option and specify its ``--help``
text, but in ``hmp-commands.hx`` the C code sections are elements
of an array of structs of type ``HMPCommand`` which define the
name, behaviour and help text for each monitor command.

In the file ``qemu-options.hx``, do not try to explicitly define a
reStructuredText label within a documentation section. This file
is included into two separate Sphinx documents, and some
versions of Sphinx will complain about the duplicate label
that results. Use the ``SRST()`` directive documented above, to
emit an unambiguous label.
