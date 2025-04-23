.. _ci:

Continuous Integration (CI)
===========================

Continuous integration (CI) requires the builds of the entire application and
the execution of a comprehensive set of automated tests every time there is a
need to commit any set of changes [1]_. The automated tests are composed
of unit, functional and other tests.

Most of QEMU's CI is run on GitLab's infrastructure although a number
of other CI services are used for specialised purposes. The most up to
date information about them and their status can be found on the
`project wiki testing page <https://wiki.qemu.org/Testing/CI>`_.

These tests are also used as gating tests before merging pull requests.
A gating test restricts the move of code from one stage to another on a
test/deployment pipeline. The step move is granted with approval. The approval
can be a manual intervention or a set of tests succeeding [2]_.

On QEMU, the gating process happens during the pull request. The approval is
done by the project leader running its own set of tests. The pull request gets
merged when the tests succeed.

.. include:: ci-jobs.rst.inc
.. include:: ci-runners.rst.inc

References
----------

.. [1] Humble, Jez & Farley, David (2010). Continuous Delivery:
       Reliable Software Releases Through Build, Test, and Deployment, p. 55.
.. [2] Humble, Jez & Farley, David (2010). Continuous Delivery:
       Reliable Software Releases Through Build, Test, and Deployment, p. 122.
