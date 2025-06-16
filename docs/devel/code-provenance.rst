.. _code-provenance:

Code provenance
===============

Certifying patch submissions
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The QEMU community **mandates** all contributors to certify provenance of
patch submissions they make to the project. To put it another way,
contributors must indicate that they are legally permitted to contribute to
the project.

Certification is achieved with a low overhead by adding a single line to the
bottom of every git commit::

   Signed-off-by: YOUR NAME <YOUR@EMAIL>

The addition of this line asserts that the author of the patch is contributing
in accordance with the clauses specified in the
`Developer's Certificate of Origin <https://developercertificate.org>`__:

.. _dco:

  Developer's Certificate of Origin 1.1

  By making a contribution to this project, I certify that:

  (a) The contribution was created in whole or in part by me and I
      have the right to submit it under the open source license
      indicated in the file; or

  (b) The contribution is based upon previous work that, to the best
      of my knowledge, is covered under an appropriate open source
      license and I have the right under that license to submit that
      work with modifications, whether created in whole or in part
      by me, under the same open source license (unless I am
      permitted to submit under a different license), as indicated
      in the file; or

  (c) The contribution was provided directly to me by some other
      person who certified (a), (b) or (c) and I have not modified
      it.

  (d) I understand and agree that this project and the contribution
      are public and that a record of the contribution (including all
      personal information I submit with it, including my sign-off) is
      maintained indefinitely and may be redistributed consistent with
      this project or the open source license(s) involved.

The name used with "Signed-off-by" does not need to be your legal name, nor
birth name, nor appear on any government ID. It is the identity you choose to
be known by in the community, but should not be anonymous, nor misrepresent
whom you are.

It is generally expected that the name and email addresses used in one of the
``Signed-off-by`` lines, matches that of the git commit ``Author`` field.
It's okay if you subscribe or contribute to the list via more than one
address, but using multiple addresses in one commit just confuses
things.

If the person sending the mail is not one of the patch authors, they are
nonetheless expected to add their own ``Signed-off-by`` to comply with the
DCO clause (c).

Multiple authorship
~~~~~~~~~~~~~~~~~~~

It is not uncommon for a patch to have contributions from multiple authors. In
this scenario, git commits will usually be expected to have a ``Signed-off-by``
line for each contributor involved in creation of the patch. Some edge cases:

  * The non-primary author's contributions were so trivial that they can be
    considered not subject to copyright. In this case the secondary authors
    need not include a ``Signed-off-by``.

    This case most commonly applies where QEMU reviewers give short snippets
    of code as suggested fixes to a patch. The reviewers don't need to have
    their own ``Signed-off-by`` added unless their code suggestion was
    unusually large, but it is common to add ``Suggested-by`` as a credit
    for non-trivial code.

  * Both contributors work for the same employer and the employer requires
    copyright assignment.

    It can be said that in this case a ``Signed-off-by`` is indicating that
    the person has permission to contribute from their employer who is the
    copyright holder. It is nonetheless still preferable to include a
    ``Signed-off-by`` for each contributor, as in some countries employees are
    not able to assign copyright to their employer, and it also covers any
    time invested outside working hours.

When multiple ``Signed-off-by`` tags are present, they should be strictly kept
in order of authorship, from oldest to newest.

Other commit tags
~~~~~~~~~~~~~~~~~

While the ``Signed-off-by`` tag is mandatory, there are a number of other tags
that are commonly used during QEMU development:

 * **``Reviewed-by``**: when a QEMU community member reviews a patch on the
   mailing list, if they consider the patch acceptable, they should send an
   email reply containing a ``Reviewed-by`` tag. Subsystem maintainers who
   review a patch should add this even if they are also adding their
   ``Signed-off-by`` to the same commit.

 * **``Acked-by``**: when a QEMU subsystem maintainer approves a patch that
   touches their subsystem, but intends to allow a different maintainer to
   queue it and send a pull request, they would send a mail containing a
   ``Acked-by`` tag. Where a patch touches multiple subsystems, ``Acked-by``
   only implies review of the maintainers' own areas of responsibility. If a
   maintainer wants to indicate they have done a full review they should use
   a ``Reviewed-by`` tag.

 * **``Tested-by``**: when a QEMU community member has functionally tested the
   behaviour of the patch in some manner, they should send an email reply
   containing a ``Tested-by`` tag.

 * **``Reported-by``**: when a QEMU community member reports a problem via the
   mailing list, or some other informal channel that is not the issue tracker,
   it is good practice to credit them by including a ``Reported-by`` tag on
   any patch fixing the issue. When the problem is reported via the GitLab
   issue tracker, however, it is sufficient to just include a link to the
   issue.

 * **``Suggested-by``**: when a reviewer or other 3rd party makes non-trivial
   suggestions for how to change a patch, it is good practice to credit them
   by including a ``Suggested-by`` tag.

Subsystem maintainer requirements
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

When a subsystem maintainer accepts a patch from a contributor, in addition to
the normal code review points, they are expected to validate the presence of
suitable ``Signed-off-by`` tags.

At the time they queue the patch in their subsystem tree, the maintainer
**must** also then add their own ``Signed-off-by`` to indicate that they have
done the aforementioned validation. This is in addition to any of their own
``Reviewed-by`` tags the subsystem maintainer may wish to include.

When the maintainer modifies the patch after pulling into their tree, they
should record their contribution.  This is typically done via a note in the
commit message, just prior to the maintainer's ``Signed-off-by``::

    Signed-off-by: Cory Contributor <cory.contributor@example.com>
    [Comment rephrased for clarity]
    Signed-off-by: Mary Maintainer <mary.maintainer@mycorp.test>


Tools for adding ``Signed-off-by``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

There are a variety of ways tools can support adding ``Signed-off-by`` tags
for patches, avoiding the need for contributors to manually type in this
repetitive text each time.

git commands
^^^^^^^^^^^^

When creating, or amending, a commit the ``-s`` flag to ``git commit`` will
append a suitable line matching the configured git author details.

If preparing patches using the ``git format-patch`` tool, the ``-s`` flag can
be used to append a suitable line in the emails it creates, without modifying
the local commits. Alternatively to modify all the local commits on a branch::

  git rebase master -x 'git commit --amend --no-edit -s'

emacs
^^^^^

In the file ``$HOME/.emacs.d/abbrev_defs`` add:

.. code:: elisp

  (define-abbrev-table 'global-abbrev-table
    '(
      ("8rev" "Reviewed-by: YOUR NAME <your@email.addr>" nil 1)
      ("8ack" "Acked-by: YOUR NAME <your@email.addr>" nil 1)
      ("8test" "Tested-by: YOUR NAME <your@email.addr>" nil 1)
      ("8sob" "Signed-off-by: YOUR NAME <your@email.addr>" nil 1)
     ))

with this change, if you type (for example) ``8rev`` followed by ``<space>``
or ``<enter>`` it will expand to the whole phrase.

vim
^^^

In the file ``$HOME/.vimrc`` add::

  iabbrev 8rev Reviewed-by: YOUR NAME <your@email.addr>
  iabbrev 8ack Acked-by: YOUR NAME <your@email.addr>
  iabbrev 8test Tested-by: YOUR NAME <your@email.addr>
  iabbrev 8sob Signed-off-by: YOUR NAME <your@email.addr>

with this change, if you type (for example) ``8rev`` followed by ``<space>``
or ``<enter>`` it will expand to the whole phrase.

Re-starting abandoned work
~~~~~~~~~~~~~~~~~~~~~~~~~~

For a variety of reasons there are some patches that get submitted to QEMU but
never merged. An unrelated contributor may decide (months or years later) to
continue working from the abandoned patch and re-submit it with extra changes.

The general principles when picking up abandoned work are:

 * Continue to credit the original author for their work, by maintaining their
   original ``Signed-off-by``
 * Indicate where the original patch was obtained from (mailing list, bug
   tracker, author's git repo, etc) when sending it for review
 * Acknowledge the extra work of the new contributor by including their
   ``Signed-off-by`` in the patch in addition to the orignal author's
 * Indicate who is responsible for what parts of the patch. This is typically
   done via a note in the commit message, just prior to the new contributor's
   ``Signed-off-by``::

    Signed-off-by: Some Person <some.person@example.com>
    [Rebased and added support for 'foo']
    Signed-off-by: New Person <new.person@mycorp.test>

In complicated cases, or if otherwise unsure, ask for advice on the project
mailing list.

It is also recommended to attempt to contact the original author to let them
know you are interested in taking over their work, in case they still intended
to return to the work, or had any suggestions about the best way to continue.
