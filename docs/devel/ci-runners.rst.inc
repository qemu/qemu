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

Machine Setup Howto
-------------------

For all Linux based systems, the setup can be mostly automated by the
execution of two Ansible playbooks.  Create an ``inventory`` file
under ``scripts/ci/setup``, such as this::

  fully.qualified.domain
  other.machine.hostname

You may need to set some variables in the inventory file itself.  One
very common need is to tell Ansible to use a Python 3 interpreter on
those hosts.  This would look like::

  fully.qualified.domain ansible_python_interpreter=/usr/bin/python3
  other.machine.hostname ansible_python_interpreter=/usr/bin/python3

Build environment
~~~~~~~~~~~~~~~~~

The ``scripts/ci/setup/build-environment.yml`` Ansible playbook will
set up machines with the environment needed to perform builds and run
QEMU tests.  This playbook consists on the installation of various
required packages (and a general package update while at it).  It
currently covers a number of different Linux distributions, but it can
be expanded to cover other systems.

The minimum required version of Ansible successfully tested in this
playbook is 2.8.0 (a version check is embedded within the playbook
itself).  To run the playbook, execute::

  cd scripts/ci/setup
  ansible-playbook -i inventory build-environment.yml

Please note that most of the tasks in the playbook require superuser
privileges, such as those from the ``root`` account or those obtained
by ``sudo``.  If necessary, please refer to ``ansible-playbook``
options such as ``--become``, ``--become-method``, ``--become-user``
and ``--ask-become-pass``.

gitlab-runner setup and registration
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The gitlab-runner agent needs to be installed on each machine that
will run jobs.  The association between a machine and a GitLab project
happens with a registration token.  To find the registration token for
your repository/project, navigate on GitLab's web UI to:

 * Settings (the gears-like icon at the bottom of the left hand side
   vertical toolbar), then
 * CI/CD, then
 * Runners, and click on the "Expand" button, then
 * Under "Set up a specific Runner manually", look for the value under
   "And this registration token:"

Copy the ``scripts/ci/setup/vars.yml.template`` file to
``scripts/ci/setup/vars.yml``.  Then, set the
``gitlab_runner_registration_token`` variable to the value obtained
earlier.

To run the playbook, execute::

  cd scripts/ci/setup
  ansible-playbook -i inventory gitlab-runner.yml

Following the registration, it's necessary to configure the runner tags,
and optionally other configurations on the GitLab UI.  Navigate to:

 * Settings (the gears like icon), then
 * CI/CD, then
 * Runners, and click on the "Expand" button, then
 * "Runners activated for this project", then
 * Click on the "Edit" icon (next to the "Lock" Icon)

Tags are very important as they are used to route specific jobs to
specific types of runners, so it's a good idea to double check that
the automatically created tags are consistent with the OS and
architecture.  For instance, an Ubuntu 20.04 aarch64 system should
have tags set as::

  ubuntu_20.04,aarch64

Because the job definition at ``.gitlab-ci.d/custom-runners.yml``
would contain::

  ubuntu-20.04-aarch64-all:
   tags:
   - ubuntu_20.04
   - aarch64

It's also recommended to:

 * increase the "Maximum job timeout" to something like ``2h``
 * give it a better Description
