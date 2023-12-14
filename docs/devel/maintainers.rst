.. _maintainers:

The Role of Maintainers
=======================

Maintainers are a critical part of the project's contributor ecosystem.
They come from a wide range of backgrounds from unpaid hobbyists
working in their spare time to employees who work on the project as
part of their job. Maintainer activities include:

  - reviewing patches and suggesting changes
  - collecting patches and preparing pull requests
  - tending to the long term health of their area
  - participating in other project activities

They are also human and subject to the same pressures as everyone else
including overload and burnout. Like everyone else they are subject
to project's :ref:`code_of_conduct` and should also be exemplars of
excellent community collaborators.

The MAINTAINERS file
--------------------

The `MAINTAINERS
<https://gitlab.com/qemu-project/qemu/-/blob/master/MAINTAINERS>`__
file contains the canonical list of who is a maintainer. The file
is machine readable so an appropriately configured git (see
:ref:`cc_the_relevant_maintainer`) can automatically Cc them on
patches that touch their area of code.

The file also describes the status of the area of code to give an idea
of how actively that section is maintained.

.. list-table:: Meaning of support status in MAINTAINERS
   :widths: 25 75
   :header-rows: 1

   * - Status
     - Meaning
   * - Supported
     - Someone is actually paid to look after this.
   * - Maintained
     - Someone actually looks after it.
   * - Odd Fixes
     - It has a maintainer but they don't have time to do
       much other than throw the odd patch in.
   * - Orphan
     - No current maintainer.
   * - Obsolete
     - Old obsolete code, should use something else.

Please bear in mind that even if someone is paid to support something
it does not mean they are paid to support you. This is open source and
the code comes with no warranty and the project makes no guarantees
about dealing with bugs or features requests.



Becoming a reviewer
-------------------

Most maintainers start by becoming subsystem reviewers. While anyone
is welcome to review code on the mailing list getting added to the
MAINTAINERS file with a line like::

  R: Random Hacker <rhacker@example.com>

marks you as a 'designated reviewer' - expected to provide regular
spontaneous feedback. This will ensure that patches touching a given
subsystem will automatically be CC'd to you.

Becoming a maintainer
---------------------

Maintainers are volunteers who put themselves forward or have been
asked by others to keep an eye on an area of code. They have generally
demonstrated to the community, usually via contributions and code
reviews, that they have a good understanding of the subsystem. They
are also trusted to make a positive contribution to the project and
work well with the other contributors.

The process is simple - simply send a patch to the list that updates
the ``MAINTAINERS`` file. Sometimes this is done as part of a larger
series when a new sub-system is being added to the code base. This can
also be done by a retiring maintainer who nominates their replacement
after discussion with other contributors.

Once the patch is reviewed and merged the only other step is to make
sure your GPG key is signed.

.. _maintainer_keys:

Maintainer GPG Keys
~~~~~~~~~~~~~~~~~~~

GPG is used to sign pull requests so they can be identified as really
coming from the maintainer. If your key is not already signed by
members of the QEMU community, you should make arrangements to attend
a `KeySigningParty <https://wiki.qemu.org/KeySigningParty>`__ (for
example at KVM Forum) or make alternative arrangements to have your
key signed by an attendee. Key signing requires meeting another
community member **in person** [#]_ so please make appropriate
arrangements.

.. [#] In recent pandemic times we have had to exercise some
       flexibility here. Maintainers still need to sign their pull
       requests though.
