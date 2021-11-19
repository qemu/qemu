.. _stable-process:

QEMU and the stable process
===========================

QEMU stable releases
--------------------

QEMU stable releases are based upon the last released QEMU version
and marked by an additional version number, e.g. 2.10.1. Occasionally,
a four-number version is released, if a single urgent fix needs to go
on top.

Usually, stable releases are only provided for the last major QEMU
release. For example, when QEMU 2.11.0 is released, 2.11.x or 2.11.x.y
stable releases are produced only until QEMU 2.12.0 is released, at
which point the stable process moves to producing 2.12.x/2.12.x.y releases.

What should go into a stable release?
-------------------------------------

Generally, the following patches are considered stable material:

* Patches that fix severe issues, like fixes for CVEs

* Patches that fix regressions

If you think the patch would be important for users of the current release
(or for a distribution picking fixes), it is usually a good candidate
for stable.


How to get a patch into QEMU stable
-----------------------------------

There are various ways to get a patch into stable:

* Preferred: Make sure that the stable maintainers are on copy when you send
  the patch by adding

  .. code::

     Cc: qemu-stable@nongnu.org

  to the patch description. By default, this will send a copy of the patch
  to ``qemu-stable@nongnu.org`` if you use git send-email, which is where
  patches that are stable candidates are tracked by the maintainers.

* You can also reply to a patch and put ``qemu-stable@nongnu.org`` on copy
  directly in your mail client if you think a previously submitted patch
  should be considered for a stable release.

* If a maintainer judges the patch appropriate for stable later on (or you
  notify them), they will add the same line to the patch, meaning that
  the stable maintainers will be on copy on the maintainer's pull request.

* If you judge an already merged patch suitable for stable, send a mail
  (preferably as a reply to the most recent patch submission) to
  ``qemu-stable@nongnu.org`` along with ``qemu-devel@nongnu.org`` and
  appropriate other people (like the patch author or the relevant maintainer)
  on copy.

Stable release process
----------------------

When the stable maintainers prepare a new stable release, they will prepare
a git branch with a release candidate and send the patches out to
``qemu-devel@nongnu.org`` for review. If any of your patches are included,
please verify that they look fine, especially if the maintainer had to tweak
the patch as part of back-porting things across branches. You may also
nominate other patches that you think are suitable for inclusion. After
review is complete (may involve more release candidates), a new stable release
is made available.
