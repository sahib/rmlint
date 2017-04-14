Testsuite
---------

``rmlint`` has a not yet complete but quite powerful testsuite. It is not
complete yet (and probably never will), but it's already a valuable boost of
confidence in ``rmlint's`` correctness.

The tests are based on ``nosetest`` and are written in ``python>=3.0``.
Every testcase just runs the (previously built) ``rmlint`` binary a
and parses its json output. So they are technically blackbox-tests.

On every commit, those tests are additionally run on `TravisCI`_.

.. _`TravisCI`: https://travis-ci.org/sahib/rmlint

Control Variables
~~~~~~~~~~~~~~~~~

The behaviour of the testsuite can be controlled by certain environment
variables which are:

- ``RM_TS_DIR``: Testdir to create files in. Can be very large with some tests,
  sometimes ``tmpfs`` might therefore slow down your computer. By default
  ``/tmp`` will be used.
- ``RM_TS_USE_VALGRIND``: Run each test inside of valgrind's memcheck. *(slow)*
- ``RM_TS_CHECK_LEAKS``: Fail test if valgrind indicates (definite) memory leak.
- ``RM_TS_USE_GDB``: Run tests inside of ``gdb``. Fatal signals will trigger a
  backtrace.
- ``RM_TS_PEDANTIC``: Run each test several times with different optimization options
  and check for errors between the runs. *(slow)*.
- ``RM_TS_SLEEP``: Waits a long time before executing a command. Useful for
  starting the testcase and manually running `rmlint` on the priorly generated
  testdir. 
- ``RM_TS_PRINT_CMD``: Print the command that is currently run.
- ``RM_TS_KEEP_TESTDIR``: If a test failed, keep the test files.

Additionally slow tests can be omitted with by appending ``-a '!slow'`` to 
the commandline. More information on this syntax can be found on the `nosetest
documentation`_.

.. _`nosetest documentation`: http://nose.readthedocs.org/en/latest/plugins/attrib.html

Before each release we call the testsuite (at least) like this:

.. code-block:: bash

   $ sudo RM_TS_USE_VALGRIND=1 RM_TS_PRINT_CMD=1 RM_TS_PEDANTIC=1 nosetests-3.4 -s -a '!slow' 

The ``sudo`` here is there for executing some tests that need root access (like
the creating of bad user and group ids). Most tests will work without.

Coverage
~~~~~~~~

To see which functions need more testcases we use ``gcov`` to detect which lines
were executed (and how often) by the testsuite. Here's a short quickstart using
``lcov``:

.. code-block:: bash

    $ CFLAGS="-fprofile-arcs -ftest-coverage" LDFLAGS="-fprofile-arcs -ftest-coverage" scons -j4 DEBUG=1
    $ sudo RM_TS_USE_VALGRIND=1 RM_TS_PRINT_CMD=1 RM_TS_PEDANTIC=1 nosetests-3.4 -s -a '!slow'
    $ lcov --capture --directory . --output-file coverage.info
    $ genhtml coverage.info --output-directory out

The coverage results are updated from time to time here:

    http://sahib.github.io/rmlint/gcov/index.html

Structure
~~~~~~~~~

.. code-block:: bash

    tests
    ├── test_formatters   # Tests for output formatters (like sh or json)
    ├── test_options      # Tests for normal options like --merge-directories etc.
    ├── test_types        # Tests for all lint types rmlint can find
    └── utils.py          # Common utilities shared among tests.

Templates
~~~~~~~~~

A template for a testcase looks like this:

.. code-block:: python

    from nose import with_setup
    from tests.utils import *

    @with_setup(usual_setup_func, usual_teardown_func)
    def test_basic():
        create_file('xxx', 'a')
        create_file('xxx', 'b')

        head, *data, footer = run_rmlint('-a city -S a')

        assert footer['duplicate_sets'] == 1
        assert footer['total_lint_size'] == 3
        assert footer['total_files'] == 2
        assert footer['duplicates'] == 1

Rules
~~~~~

* Test should be able to run as normal user.
* If that's not possible, check at the beginning of the testcase with this:

  .. code-block:: python

      if not runs_as_root():
          return

* Regressions in ``rmlint`` should get their own testcase so they do not
  appear again. 
* Slow tests can be marked with a slow attribute: 

  .. code-block:: python

    from nose.plugins.attrib import attr

    @attr('slow')
    @with_setup(usual_setup_func, usual_teardown_func)
    def test_debian_support():
        assert random.choice([True, False]):
