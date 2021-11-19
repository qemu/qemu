.. _trivial-patches:

Trivial Patches
===============

Overview
--------

Trivial patches that change just a few lines of code sometimes languish
on the mailing list even though they require only a small amount of
review. This is often the case for patches that do not fall under an
actively maintained subsystem and therefore fall through the cracks.

The trivial patches team take on the task of reviewing and building pull
requests for patches that:

- Do not fall under an actively maintained subsystem.
- Are single patches or short series (max 2-4 patches).
- Only touch a few lines of code.

**You should hint that your patch is a candidate by CCing
qemu-trivial@nongnu.org.**

Repositories
------------

Since the trivial patch team rotates maintainership there is only one
active repository at a time:

- git://github.com/vivier/qemu.git trivial-patches - `browse <https://github.com/vivier/qemu/tree/trivial-patches>`__

Workflow
--------

The trivial patches team rotates the duty of collecting trivial patches
amongst its members. A team member's job is to:

1. Identify trivial patches on the development mailing list.
2. Review trivial patches, merge them into a git tree, and reply to state
   that the patch is queued.
3. Send pull requests to the development mailing list once a week.

A single team member can be on duty as long as they like. The suggested
time is 1 week before handing off to the next member.

Team
----

If you would like to join the trivial patches team, contact Laurent
Vivier. The current team includes:

- `Laurent Vivier <mailto:laurent@vivier.eu>`__
