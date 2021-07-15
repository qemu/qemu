Cirrus CI integration
=====================

GitLab CI shared runners only provide a docker environment running on Linux.
While it is possible to provide private runners for non-Linux platforms this
is not something most contributors/maintainers will wish to do.

To work around this limitation, we take advantage of `Cirrus CI`_'s free
offering: more specifically, we use the `cirrus-run`_ script to trigger Cirrus
CI jobs from GitLab CI jobs so that Cirrus CI job output is integrated into
the main GitLab CI pipeline dashboard.

There is, however, some one-time setup required. If you want FreeBSD and macOS
builds to happen when you push to your GitLab repository, you need to

* set up a GitHub repository for the project, eg. ``yourusername/qemu``.
  This repository needs to exist for cirrus-run to work, but it doesn't need to
  be kept up to date, so you can create it and then forget about it;

* enable the `Cirrus CI GitHub app`_  for your GitHub account;

* sign up for Cirrus CI. It's enough to log into the website using your GitHub
  account;

* grab an API token from the `Cirrus CI settings`_ page;

* it may be necessary to push an empty ``.cirrus.yml`` file to your github fork
  for Cirrus CI to properly recognize the project. You can check whether
  Cirrus CI knows about your project by navigating to:

  ``https://cirrus-ci.com/yourusername/qemu``

* in the *CI/CD / Variables* section of the settings page for your GitLab
  repository, create two new variables:

  * ``CIRRUS_GITHUB_REPO``, containing the name of the GitHub repository
    created earlier, eg. ``yourusername/qemu``;

  * ``CIRRUS_API_TOKEN``, containing the Cirrus CI API token generated earlier.
    This variable **must** be marked as *Masked*, because anyone with knowledge
    of it can impersonate you as far as Cirrus CI is concerned.

  Neither of these variables should be marked as *Protected*, because in
  general you'll want to be able to trigger Cirrus CI builds from non-protected
  branches.

Once this one-time setup is complete, you can just keep pushing to your GitLab
repository as usual and you'll automatically get the additional CI coverage.


.. _Cirrus CI GitHub app: https://github.com/marketplace/cirrus-ci
.. _Cirrus CI settings: https://cirrus-ci.com/settings/profile/
.. _Cirrus CI: https://cirrus-ci.com/
.. _cirrus-run: https://github.com/sio/cirrus-run/
