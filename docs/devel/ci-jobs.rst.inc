Custom CI/CD variables
======================

QEMU CI pipelines can be tuned by setting some CI environment variables.

Set variable globally in the user's CI namespace
------------------------------------------------

Variables can be set globally in the user's CI namespace setting.

For further information about how to set these variables, please refer to::

  https://docs.gitlab.com/ee/ci/variables/#add-a-cicd-variable-to-a-project

Set variable manually when pushing a branch or tag to the user's repository
---------------------------------------------------------------------------

Variables can be set manually when pushing a branch or tag, using
git-push command line arguments.

Example setting the QEMU_CI_EXAMPLE_VAR variable:

.. code::

   git push -o ci.variable="QEMU_CI_EXAMPLE_VAR=value" myrepo mybranch

For further information about how to set these variables, please refer to::

  https://docs.gitlab.com/ee/user/project/push_options.html#push-options-for-gitlab-cicd

Variable naming and grouping
----------------------------

The variables used by QEMU's CI configuration are grouped together
in a handful of namespaces

 * QEMU_JOB_nnnn - variables to be defined in individual jobs
   or templates, to influence the shared rules defined in the
   .base_job_template.

 * QEMU_CI_nnn - variables to be set by contributors in their
   repository CI settings, or as git push variables, to influence
   which jobs get run in a pipeline

 * nnn - other misc variables not falling into the above
   categories, or using different names for historical reasons
   and not yet converted.

Maintainer controlled job variables
-----------------------------------

The following variables may be set when defining a job in the
CI configuration file.

QEMU_JOB_CIRRUS
~~~~~~~~~~~~~~~

The job makes use of Cirrus CI infrastructure, requiring the
configuration setup for cirrus-run to be present in the repository

QEMU_JOB_OPTIONAL
~~~~~~~~~~~~~~~~~

The job is expected to be successful in general, but is not run
by default due to need to conserve limited CI resources. It is
available to be started manually by the contributor in the CI
pipelines UI.

QEMU_JOB_ONLY_FORKS
~~~~~~~~~~~~~~~~~~~

The job results are only of interest to contributors prior to
submitting code. They are not required as part of the gating
CI pipeline.

Contributor controlled runtime variables
----------------------------------------

The following variables may be set by contributors to control
job execution

QEMU_CI_AVOCADO_TESTING
~~~~~~~~~~~~~~~~~~~~~~~
By default, tests using the Avocado framework are not run automatically in
the pipelines (because multiple artifacts have to be downloaded, and if
these artifacts are not already cached, downloading them make the jobs
reach the timeout limit). Set this variable to have the tests using the
Avocado framework run automatically.

Other misc variables
--------------------

These variables are primarily to control execution of jobs on
private runners

AARCH64_RUNNER_AVAILABLE
~~~~~~~~~~~~~~~~~~~~~~~~
If you've got access to an aarch64 host that can be used as a gitlab-CI
runner, you can set this variable to enable the tests that require this
kind of host. The runner should be tagged with "aarch64".

AARCH32_RUNNER_AVAILABLE
~~~~~~~~~~~~~~~~~~~~~~~~
If you've got access to an armhf host or an arch64 host that can run
aarch32 EL0 code to be used as a gitlab-CI runner, you can set this
variable to enable the tests that require this kind of host. The
runner should be tagged with "aarch32".

S390X_RUNNER_AVAILABLE
~~~~~~~~~~~~~~~~~~~~~~
If you've got access to an IBM Z host that can be used as a gitlab-CI
runner, you can set this variable to enable the tests that require this
kind of host. The runner should be tagged with "s390x".

CENTOS_STREAM_8_x86_64_RUNNER_AVAILABLE
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
If you've got access to a CentOS Stream 8 x86_64 host that can be
used as a gitlab-CI runner, you can set this variable to enable the
tests that require this kind of host. The runner should be tagged with
both "centos_stream_8" and "x86_64".
