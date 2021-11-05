Definition of terms
===================

This section defines the terms used in this document and correlates them with
what is currently used on QEMU.

Automated tests
---------------

An automated test is written on a test framework using its generic test
functions/classes. The test framework can run the tests and report their
success or failure [1]_.

An automated test has essentially three parts:

1. The test initialization of the parameters, where the expected parameters,
   like inputs and expected results, are set up;
2. The call to the code that should be tested;
3. An assertion, comparing the result from the previous call with the expected
   result set during the initialization of the parameters. If the result
   matches the expected result, the test has been successful; otherwise, it has
   failed.

Unit testing
------------

A unit test is responsible for exercising individual software components as a
unit, like interfaces, data structures, and functionality, uncovering errors
within the boundaries of a component. The verification effort is in the
smallest software unit and focuses on the internal processing logic and data
structures. A test case of unit tests should be designed to uncover errors due
to erroneous computations, incorrect comparisons, or improper control flow [2]_.

On QEMU, unit testing is represented by the 'check-unit' target from 'make'.

Functional testing
------------------

A functional test focuses on the functional requirement of the software.
Deriving sets of input conditions, the functional tests should fully exercise
all the functional requirements for a program. Functional testing is
complementary to other testing techniques, attempting to find errors like
incorrect or missing functions, interface errors, behavior errors, and
initialization and termination errors [3]_.

On QEMU, functional testing is represented by the 'check-qtest' target from
'make'.

System testing
--------------

System tests ensure all application elements mesh properly while the overall
functionality and performance are achieved [4]_. Some or all system components
are integrated to create a complete system to be tested as a whole. System
testing ensures that components are compatible, interact correctly, and
transfer the right data at the right time across their interfaces. As system
testing focuses on interactions, use case-based testing is a practical approach
to system testing [5]_. Note that, in some cases, system testing may require
interaction with third-party software, like operating system images, databases,
networks, and so on.

On QEMU, system testing is represented by the 'check-avocado' target from
'make'.

Flaky tests
-----------

A flaky test is defined as a test that exhibits both a passing and a failing
result with the same code on different runs. Some usual reasons for an
intermittent/flaky test are async wait, concurrency, and test order dependency
[6]_.

Gating
------

A gate restricts the move of code from one stage to another on a
test/deployment pipeline. The step move is granted with approval. The approval
can be a manual intervention or a set of tests succeeding [7]_.

On QEMU, the gating process happens during the pull request. The approval is
done by the project leader running its own set of tests. The pull request gets
merged when the tests succeed.

Continuous Integration (CI)
---------------------------

Continuous integration (CI) requires the builds of the entire application and
the execution of a comprehensive set of automated tests every time there is a
need to commit any set of changes [8]_. The automated tests can be composed of
the unit, functional, system, and other tests.

Keynotes about continuous integration (CI) [9]_:

1. System tests may depend on external software (operating system images,
   firmware, database, network).
2. It may take a long time to build and test. It may be impractical to build
   the system being developed several times per day.
3. If the development platform is different from the target platform, it may
   not be possible to run system tests in the developer’s private workspace.
   There may be differences in hardware, operating system, or installed
   software. Therefore, more time is required for testing the system.

References
----------

.. [1] Sommerville, Ian (2016). Software Engineering. p. 233.
.. [2] Pressman, Roger S. & Maxim, Bruce R. (2020). Software Engineering,
       A Practitioner’s Approach. p. 48, 376, 378, 381.
.. [3] Pressman, Roger S. & Maxim, Bruce R. (2020). Software Engineering,
       A Practitioner’s Approach. p. 388.
.. [4] Pressman, Roger S. & Maxim, Bruce R. (2020). Software Engineering,
       A Practitioner’s Approach. Software Engineering, p. 377.
.. [5] Sommerville, Ian (2016). Software Engineering. p. 59, 232, 240.
.. [6] Luo, Qingzhou, et al. An empirical analysis of flaky tests.
       Proceedings of the 22nd ACM SIGSOFT International Symposium on
       Foundations of Software Engineering. 2014.
.. [7] Humble, Jez & Farley, David (2010). Continuous Delivery:
       Reliable Software Releases Through Build, Test, and Deployment, p. 122.
.. [8] Humble, Jez & Farley, David (2010). Continuous Delivery:
       Reliable Software Releases Through Build, Test, and Deployment, p. 55.
.. [9] Sommerville, Ian (2016). Software Engineering. p. 743.
