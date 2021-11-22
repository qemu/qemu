.. _submitting-a-pull-request:

Submitting a Pull Request
=========================

QEMU welcomes contributions of code, but we generally expect these to be
sent as simple patch emails to the mailing list (see our page on
:ref:`submitting-a-patch`
for more details).  Generally only existing submaintainers of a tree
will need to submit pull requests, although occasionally for a large
patch series we might ask a submitter to send a pull request. This page
documents our recommendations on pull requests for those people.

A good rule of thumb is not to send a pull request unless somebody asks
you to.

**Resend the patches with the pull request** as emails which are
threaded as follow-ups to the pull request itself. The simplest way to
do this is to use ``git format-patch --cover-letter`` to create the
emails, and then edit the cover letter to include the pull request
details that ``git request-pull`` outputs.

**Use PULL as the subject line tag** in both the cover letter and the
retransmitted patch mails (for example, by using
``--subject-prefix=PULL`` in your ``git format-patch`` command). This
helps people to filter in or out the resulting emails (especially useful
if they are only CC'd on one email out of the set).

**Each patch must have your own Signed-off-by: line** as well as that of
the original author if the patch was not written by you. This is because
with a pull request you're now indicating that the patch has passed via
you rather than directly from the original author.

**Don't forget to add Reviewed-by: and Acked-by: lines**. When other
people have reviewed the patches you're putting in the pull request,
make sure you've copied their signoffs across. (If you use the `patches
tool <https://github.com/stefanha/patches>`__ to add patches from email
directly to your git repo it will include the tags automatically; if
you're updating patches manually or in some other way you'll need to
edit the commit messages by hand.)

**Don't send pull requests for code that hasn't passed review**. A pull
request says these patches are ready to go into QEMU now, so they must
have passed the standard code review processes. In particular if you've
corrected issues in one round of code review, you need to send your
fixed patch series as normal to the list; you can't put it in a pull
request until it's gone through. (Extremely trivial fixes may be OK to
just fix in passing, but if in doubt err on the side of not.)

**Test before sending**. This is an obvious thing to say, but make sure
everything builds (including that it compiles at each step of the patch
series) and that "make check" passes before sending out the pull
request. As a submaintainer you're one of QEMU's lines of defense
against bad code, so double check the details.

**All pull requests must be signed**. If your key is not already signed
by members of the QEMU community, you should make arrangements to attend
a `KeySigningParty <https://wiki.qemu.org/KeySigningParty>`__ (for
example at KVM Forum) or make alternative arrangements to have your key
signed by an attendee.  Key signing requires meeting another community
member \*in person\* so please make appropriate arrangements.  By
"signed" here we mean that the pullreq email should quote a tag which is
a GPG-signed tag (as created with 'gpg tag -s ...').

**Pull requests not for master should say "not for master" and have
"PULL SUBSYSTEM whatever" in the subject tag**. If your pull request is
targeting a stable branch or some submaintainer tree, please include the
string "not for master" in the cover letter email, and make sure the
subject tag is "PULL SUBSYSTEM s390/block/whatever" rather than just
"PULL". This allows it to be automatically filtered out of the set of
pull requests that should be applied to master.

You might be interested in the `make-pullreq
<https://git.linaro.org/people/peter.maydell/misc-scripts.git/tree/make-pullreq>`__
script which automates some of this process for you and includes a few
sanity checks. Note that you must edit it to configure it suitably for
your local situation!
