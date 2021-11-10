Submitting a Patch
==================

QEMU welcomes contributions of code (either fixing bugs or adding new
functionality). However, we get a lot of patches, and so we have some
guidelines about submitting patches. If you follow these, you'll help
make our task of code review easier and your patch is likely to be
committed faster.

This page seems very long, so if you are only trying to post a quick
one-shot fix, the bare minimum we ask is that:

-  You **must** provide a Signed-off-by: line (this is a hard
   requirement because it's how you say "I'm legally okay to contribute
   this and happy for it to go into QEMU", modeled after the `Linux kernel
   <http://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/tree/Documentation/SubmittingPatches?id=f6f94e2ab1b33f0082ac22d71f66385a60d8157f#n297>`__
   policy.) ``git commit -s`` or ``git format-patch -s`` will add one.
-  All contributions to QEMU must be **sent as patches** to the
   qemu-devel `mailing list <MailingLists>`__. Patch contributions
   should not be posted on the bug tracker, posted on forums, or
   externally hosted and linked to. (We have other mailing lists too,
   but all patches must go to qemu-devel, possibly with a Cc: to another
   list.) ``git send-email`` works best for delivering the patch without
   mangling it (`hints for setting it
   up <http://lxr.free-electrons.com/source/Documentation/process/email-clients.rst>`__),
   but attachments can be used as a last resort on a first-time
   submission.
-  You must read replies to your message, and be willing to act on them.
   Note, however, that maintainers are often willing to manually fix up
   first-time contributions, since there is a learning curve involved in
   making an ideal patch submission.

You do not have to subscribe to post (list policy is to reply-to-all to
preserve CCs and keep non-subscribers in the loop on the threads they
start), although you may find it easier as a subscriber to pick up good
ideas from other posts. If you do subscribe, be prepared for a high
volume of email, often over one thousand messages in a week. The list is
moderated; first-time posts from an email address (whether or not you
subscribed) may be subject to some delay while waiting for a moderator
to whitelist your address.

The larger your contribution is, or if you plan on becoming a long-term
contributor, then the more important the rest of this page becomes.
Reading the table of contents below should already give you an idea of
the basic requirements. Use the table of contents as a reference, and
read the parts that you have doubts about.

.. _writing_your_patches:

Writing your Patches
--------------------

.. _use_the_qemu_coding_style:

Use the QEMU coding style
~~~~~~~~~~~~~~~~~~~~~~~~~

You can run run *scripts/checkpatch.pl <patchfile>* before submitting to
check that you are in compliance with our coding standards. Be aware
that ``checkpatch.pl`` is not infallible, though, especially where C
preprocessor macros are involved; use some common sense too. See also:

- `QEMU Coding Style
  <https://qemu-project.gitlab.io/qemu/devel/style.html>`__

-  `Automate a checkpatch run on
   commit <http://blog.vmsplice.net/2011/03/how-to-automatically-run-checkpatchpl.html>`__

.. _base_patches_against_current_git_master:

Base patches against current git master
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

There's no point submitting a patch which is based on a released version
of QEMU because development will have moved on from then and it probably
won't even apply to master. We only apply selected bugfixes to release
branches and then only as backports once the code has gone into master.

.. _split_up_long_patches:

Split up long patches
~~~~~~~~~~~~~~~~~~~~~

Split up longer patches into a patch series of logical code changes.
Each change should compile and execute successfully. For instance, don't
add a file to the makefile in patch one and then add the file itself in
patch two. (This rule is here so that people can later use tools like
`git bisect <http://git-scm.com/docs/git-bisect>`__ without hitting
points in the commit history where QEMU doesn't work for reasons
unrelated to the bug they're chasing.) Put documentation first, not
last, so that someone reading the series can do a clean-room evaluation
of the documentation, then validate that the code matched the
documentation. A commit message that mentions "Also, ..." is often a
good candidate for splitting into multiple patches. For more thoughts on
properly splitting patches and writing good commit messages, see `this
advice from
OpenStack <https://wiki.openstack.org/wiki/GitCommitMessages>`__.

.. _make_code_motion_patches_easy_to_review:

Make code motion patches easy to review
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

If a series requires large blocks of code motion, there are tricks for
making the refactoring easier to review. Split up the series so that
semantic changes (or even function renames) are done in a separate patch
from the raw code motion. Use a one-time setup of
``git config diff.renames true; git config diff.algorithm patience``
(Refer to `git-config <http://git-scm.com/docs/git-config>`__.)  The
``diff.renames`` property ensures file rename patches will be given in a
more compact representation that focuses only on the differences across
the file rename, instead of showing the entire old file as a deletion
and the new file as an insertion. Meanwhile, the 'diff.algorithm'
property ensures that extracting a non-contiguous subset of one file
into a new file, but where all extracted parts occur in the same order
both before and after the patch, will reduce churn in trying to treat
unrelated ``}`` lines in the original file as separating hunks of
changes.

Ideally, a code motion patch can be reviewed by doing::

    git format-patch --stdout -1 > patch;
    diff -u <(sed -n 's/^-//p' patch) <(sed -n 's/^\+//p' patch)

to focus on the few changes that weren't wholesale code motion.

.. _dont_include_irrelevant_changes:

Don't include irrelevant changes
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

In particular, don't include formatting, coding style or whitespace
changes to bits of code that would otherwise not be touched by the
patch. (It's OK to fix coding style issues in the immediate area (few
lines) of the lines you're changing.) If you think a section of code
really does need a reindent or other large-scale style fix, submit this
as a separate patch which makes no semantic changes; don't put it in the
same patch as your bug fix.

For smaller patches in less frequently changed areas of QEMU, consider
using the `trivial patches process
<https://qemu-project.gitlab.io/qemu/devel/style.html>`__.

.. _write_a_meaningful_commit_message:

Write a meaningful commit message
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Commit messages should be meaningful and should stand on their own as a
historical record of why the changes you applied were necessary or
useful.

QEMU follows the usual standard for git commit messages: the first line
(which becomes the email subject line) is "subsystem: single line
summary of change". Whether the "single line summary of change" starts
with a capital is a matter of taste, but we prefer that the summary does
not end in ".". Look at ``git shortlog -30`` for an idea of sample
subject lines. Then there is a blank line and a more detailed
description of the patch, another blank and your Signed-off-by: line.
Please do not use lines that are longer than 76 characters in your
commit message (so that the text still shows up nicely with "git show"
in a 80-columns terminal window).

The body of the commit message is a good place to document why your
change is important. Don't include comments like "This is a suggestion
for fixing this bug" (they can go below the ``---`` line in the email so
they don't go into the final commit message). Make sure the body of the
commit message can be read in isolation even if the reader's mailer
displays the subject line some distance apart (that is, a body that
starts with "... so that" as a continuation of the subject line is
harder to follow).

.. _submitting_your_patches:

Submitting your Patches
-----------------------

.. _cc_the_relevant_maintainer:

CC the relevant maintainer
~~~~~~~~~~~~~~~~~~~~~~~~~~

Send patches both to the mailing list and CC the maintainer(s) of the
files you are modifying. look in the MAINTAINERS file to find out who
that is. Also try using scripts/get_maintainer.pl from the repository
for learning the most common committers for the files you touched.

Example::

    ~/src/qemu/scripts/get_maintainer.pl -f hw/ide/core.c

In fact, you can automate this, via a one-time setup of ``git config
sendemail.cccmd 'scripts/get_maintainer.pl --nogit-fallback'`` (Refer to
`git-config <http://git-scm.com/docs/git-config>`__.)

.. _do_not_send_as_an_attachment:

Do not send as an attachment
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Send patches inline so they are easy to reply to with review comments.
Do not put patches in attachments.

.. _use_git_format_patch:

Use ``git format-patch``
~~~~~~~~~~~~~~~~~~~~~~~~

Use the right diff format.
`git format-patch <http://git-scm.com/docs/git-format-patch>`__ will
produce patch emails in the right format (check the documentation to
find out how to drive it). You can then edit the cover letter before
using ``git send-email`` to mail the files to the mailing list. (We
recommend `git send-email <http://git-scm.com/docs/git-send-email>`__
because mail clients often mangle patches by wrapping long lines or
messing up whitespace. Some distributions do not include send-email in a
default install of git; you may need to download additional packages,
such as 'git-email' on Fedora-based systems.) Patch series need a cover
letter, with shallow threading (all patches in the series are
in-reply-to the cover letter, but not to each other); single unrelated
patches do not need a cover letter (but if you do send a cover letter,
use --numbered so the cover and the patch have distinct subject lines).
Patches are easier to find if they start a new top-level thread, rather
than being buried in-reply-to another existing thread.

.. _patch_emails_must_include_a_signed_off_by_line:

Patch emails must include a ``Signed-off-by:`` line
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

For more information see `1.12) Sign your work
<http://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/tree/Documentation/SubmittingPatches?id=f6f94e2ab1b33f0082ac22d71f66385a60d8157f#n296>`__.
This is vital or we will not be able to apply your patch! Please use
your real name to sign a patch (not an alias or acronym).

If you wrote the patch, make sure your "From:" and "Signed-off-by:"
lines use the same spelling. It's okay if you subscribe or contribute to
the list via more than one address, but using multiple addresses in one
commit just confuses things. If someone else wrote the patch, git will
include a "From:" line in the body of the email (different from your
envelope From:) that will give credit to the correct author; but again,
that author's Signed-off-by: line is mandatory, with the same spelling.

.. _include_a_meaningful_cover_letter:

Include a meaningful cover letter
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

This usually applies only to a series that includes multiple patches;
the cover letter explains the overall goal of such a series.

When reviewers don't know your goal at the start of their review, they
may object to early changes that don't make sense until the end of the
series, because they do not have enough context yet at that point of
their review. A series where the goal is unclear also risks a higher
number of review-fix cycles because the reviewers haven't bought into
the idea yet. If the cover letter can explain these points to the
reviewer, the process will be smoother patches will get merged faster.
Make sure your cover letter includes a diffstat of changes made over the
entire series; potential reviewers know what files they are interested
in, and they need an easy way determine if your series touches them.

.. _use_the_rfc_tag_if_needed:

Use the RFC tag if needed
~~~~~~~~~~~~~~~~~~~~~~~~~

For example, "[PATCH RFC v2]". ``git format-patch --subject-prefix=RFC``
can help.

"RFC" means "Request For Comments" and is a statement that you don't
intend for your patchset to be applied to master, but would like some
review on it anyway. Reasons for doing this include:

-  the patch depends on some pending kernel changes which haven't yet
   been accepted, so the QEMU patch series is blocked until that
   dependency has been dealt with, but is worth reviewing anyway
-  the patch set is not finished yet (perhaps it doesn't cover all use
   cases or work with all targets) but you want early review of a major
   API change or design structure before continuing

In general, since it's asking other people to do review work on a
patchset that the submitter themselves is saying shouldn't be applied,
it's best to:

-  use it sparingly
-  in the cover letter, be clear about why a patch is an RFC, what areas
   of the patchset you're looking for review on, and why reviewers
   should care

.. _participating_in_code_review:

Participating in Code Review
----------------------------

All patches submitted to the QEMU project go through a code review
process before they are accepted. Some areas of code that are well
maintained may review patches quickly, lesser-loved areas of code may
have a longer delay.

.. _stay_around_to_fix_problems_raised_in_code_review:

Stay around to fix problems raised in code review
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Not many patches get into QEMU straight away -- it is quite common that
developers will identify bugs, or suggest a cleaner approach, or even
just point out code style issues or commit message typos. You'll need to
respond to these, and then send a second version of your patches with
the issues fixed. This takes a little time and effort on your part, but
if you don't do it then your changes will never get into QEMU. It's also
just polite -- it is quite disheartening for a developer to spend time
reviewing your code and suggesting improvements, only to find that
you're not going to do anything further and it was all wasted effort.

When replying to comments on your patches **reply to all and not just
the sender** -- keeping discussion on the mailing list means everybody
can follow it.

.. _pay_attention_to_review_comments:

Pay attention to review comments
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Someone took their time to review your work, and it pays to respect that
effort; repeatedly submitting a series without addressing all comments
from the previous round tends to alienate reviewers and stall your
patch. Reviewers aren't always perfect, so it is okay if you want to
argue that your code was correct in the first place instead of blindly
doing everything the reviewer asked. On the other hand, if someone
pointed out a potential issue during review, then even if your code
turns out to be correct, it's probably a sign that you should improve
your commit message and/or comments in the code explaining why the code
is correct.

If you fix issues that are raised during review **resend the entire
patch series** not just the one patch that was changed. This allows
maintainers to easily apply the fixed series without having to manually
identify which patches are relevant. Send the new version as a complete
fresh email or series of emails -- don't try to make it a followup to
version 1. (This helps automatic patch email handling tools distinguish
between v1 and v2 emails.)

.. _when_resending_patches_add_a_version_tag:

When resending patches add a version tag
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

All patches beyond the first version should include a version tag -- for
example, "[PATCH v2]". This means people can easily identify whether
they're looking at the most recent version. (The first version of a
patch need not say "v1", just [PATCH] is sufficient.) For patch series,
the version applies to the whole series -- even if you only change one
patch, you resend the entire series and mark it as "v2". Don't try to
track versions of different patches in the series separately.  `git
format-patch <http://git-scm.com/docs/git-format-patch>`__ and `git
send-email <http://git-scm.com/docs/git-send-email>`__ both understand
the ``-v2`` option to make this easier. Send each new revision as a new
top-level thread, rather than burying it in-reply-to an earlier
revision, as many reviewers are not looking inside deep threads for new
patches.

.. _include_version_history_in_patchset_revisions:

Include version history in patchset revisions
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

For later versions of patches, include a summary of changes from
previous versions, but not in the commit message itself. In an email
formatted as a git patch, the commit message is the part above the "---"
line, and this will go into the git changelog when the patch is
committed. This part should be a self-contained description of what this
version of the patch does, written to make sense to anybody who comes
back to look at this commit in git in six months' time. The part below
the "---" line and above the patch proper (git format-patch puts the
diffstat here) is a good place to put remarks for people reading the
patch email, and this is where the "changes since previous version"
summary belongs. The
`git-publish <https://github.com/stefanha/git-publish>`__ script can
help with tracking a good summary across versions. Also, the
`git-backport-diff <https://github.com/codyprime/git-scripts>`__ script
can help focus reviewers on what changed between revisions.

.. _tips_and_tricks:

Tips and Tricks
---------------

.. _proper_use_of_reviewed_by_tags_can_aid_review:

Proper use of Reviewed-by: tags can aid review
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

When reviewing a large series, a reviewer can reply to some of the
patches with a Reviewed-by tag, stating that they are happy with that
patch in isolation (sometimes conditional on minor cleanup, like fixing
whitespace, that doesn't affect code content). You should then update
those commit messages by hand to include the Reviewed-by tag, so that in
the next revision, reviewers can spot which patches were already clean
from the previous round. Conversely, if you significantly modify a patch
that was previously reviewed, remove the reviewed-by tag out of the
commit message, as well as listing the changes from the previous
version, to make it easier to focus a reviewer's attention to your
changes.

.. _if_your_patch_seems_to_have_been_ignored:

If your patch seems to have been ignored
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

If your patchset has received no replies you should "ping" it after a
week or two, by sending an email as a reply-to-all to the patch mail,
including the word "ping" and ideally also a link to the page for the
patch on
`patchwork <http://patchwork.ozlabs.org/project/qemu-devel/list/>`__ or
GMANE. It's worth double-checking for reasons why your patch might have
been ignored (forgot to CC the maintainer? annoyed people by failing to
respond to review comments on an earlier version?), but often for
less-maintained areas of QEMU patches do just slip through the cracks.
If your ping is also ignored, ping again after another week or so. As
the submitter, you are the person with the most motivation to get your
patch applied, so you have to be persistent.

.. _is_my_patch_in:

Is my patch in?
~~~~~~~~~~~~~~~

Once your patch has had enough review on list, the maintainer for that
area of code will send notification to the list that they are including
your patch in a particular staging branch. Periodically, the maintainer
then sends a `pull request
<https://qemu-project.gitlab.io/qemu/devel/submitting-a-pull-request.html>`__
for aggregating topic branches into mainline qemu. Generally, you do not
need to send a pull request unless you have contributed enough patches
to become a maintainer over a particular section of code. Maintainers
may further modify your commit, by resolving simple merge conflicts or
fixing minor typos pointed out during review, but will always add a
Signed-off-by line in addition to yours, indicating that it went through
their tree. Occasionally, the maintainer's pull request may hit more
difficult merge conflicts, where you may be requested to help rebase and
resolve the problems. It may take a couple of weeks between when your
patch first had a positive review to when it finally lands in qemu.git;
release cycle freezes may extend that time even longer.

.. _return_the_favor:

Return the favor
~~~~~~~~~~~~~~~~

Peer review only works if everyone chips in a bit of review time. If
everyone submitted more patches than they reviewed, we would have a
patch backlog. A good goal is to try to review at least as many patches
from others as what you submit. Don't worry if you don't know the code
base as well as a maintainer; it's perfectly fine to admit when your
review is weak because you are unfamiliar with the code.
