==
CI
==

QEMU has configurations enabled for a number of different CI services.
The most up to date information about them and their status can be
found at::

   https://wiki.qemu.org/Testing/CI

Jobs on Custom Runners
======================

Besides the jobs run under the various CI systems listed before, there
are a number additional jobs that will run before an actual merge.
These use the same GitLab CI's service/framework already used for all
other GitLab based CI jobs, but rely on additional systems, not the
ones provided by GitLab as "shared runners".

The architecture of GitLab's CI service allows different machines to
be set up with GitLab's "agent", called gitlab-runner, which will take
care of running jobs created by events such as a push to a branch.
Here, the combination of a machine, properly configured with GitLab's
gitlab-runner, is called a "custom runner".

The GitLab CI jobs definition for the custom runners are located under::

  .gitlab-ci.d/custom-runners.yml

Custom runners entail custom machines.  To see a list of the machines
currently deployed in the QEMU GitLab CI and their maintainers, please
refer to the QEMU `wiki <https://wiki.qemu.org/AdminContacts>`__.
