TESTS
=====

This is ``rmlint's`` test-suite. It's small and not complete yet (and will
probably never be), but it's already an valueable boost of confidence in
``rmlint's`` correctness.

The tests are based on ``nosetest`` and are written in ``python>=3.0``.
Every testcase just runs the (previously built) ``rmlint`` binary a
and parses it's json output. So they are technically blackbox-tests.

On every commit, those tests are additionally run on TravisCI:

    https://travis-ci.org/sahib/rmlint

Structure
---------

.. code-block:: bash

    tests
    ├── test_formatters   # Tests for output formatters (like sh or json)
    ├── test_options      # Tests for normal options like --merge-directories etc.
    ├── test_types        # Tests for all lint types rmlint can find
    └── utils.py          # Common utilities shared amon tests.

Templates
---------

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
-----


* Test should be able to run as normal user.
* If that's not possible, check at the beginning of the testcase with this:

  .. code-block:: python

      if not runs_as_root():
          return

* Regressions in ``rmlint`` should get their own testcase so they do not
  appear again. 
