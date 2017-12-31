#!/usr/bin/env python3
# encoding: utf-8
from nose import with_setup
from tests.utils import *


@with_setup(usual_setup_func, usual_teardown_func)
def test_just_call_it():
    create_file('1234', 'a')
    create_file('1234', 'b')

    # This test is more or less here to make sure some util functions
    # are called from our tests. We don't test any results; basically
    # only if they fatally crash or create valgrind errors.
    # Also, you shouldn't see any output on the test run.
    run_rmlint(
        '-S a', outputs=['fdupes', 'stamp', 'progressbar', 'summary', 'pretty', 'py']
    )

    # Check if the -g option does weird things. (i.e. segfault)
    subprocess.check_output(['./rmlint', '-g', '-c', 'progressbar:ascii', TESTDIR_NAME])
    subprocess.check_output(['./rmlint', '-g', '-c', 'progressbar:fancy', TESTDIR_NAME])
    subprocess.check_output(['./rmlint', '-g',  '-O' , 'fdupes', TESTDIR_NAME])
    subprocess.check_output(['./rmlint', '-g', TESTDIR_NAME])

    for silly_option in ['-pp', '-PPPP']:
        try:
            subprocess.check_output(['./rmlint', '-VVV', silly_option, TESTDIR_NAME])
        except subprocess.CalledProcessError:
            pass
        else:
            assert False
