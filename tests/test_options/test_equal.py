#!/usr/bin/env python3
# encoding: utf-8
import os
import json

from nose import with_setup
from contextlib import contextmanager

from tests.utils import *


@contextmanager
def assert_exit_code(status_code):
    """
    Assert that the with block yields a subprocess.CalledProcessError
    with a certain return code. If nothing is thrown, status_code
    is required to be 0 to survive the test.
    """
    try:
        yield
    except subprocess.CalledProcessError as exc:
        assert exc.returncode == status_code
    else:
        # No exception? status_code should be fine.
        assert status_code == 0


@with_setup(usual_setup_func, usual_teardown_func)
def test_equal_files():
    path_a = create_file('1234', 'a')
    path_b = create_file('1234', 'b')

    with assert_exit_code(0):
        head, *data, footer = run_rmlint(
            '--equal', path_a, path_b,
            use_default_dir=False
        )

    with assert_exit_code(0):
        head, *data, footer = run_rmlint(
            '-p', '--equal', path_a, path_b,
            use_default_dir=False
        )

    # If --equal finds that both files are not equal,
    # it would return 1 as exit code which would cause
    # a CalledProcessError in run_rmlint
    assert data[0]['path'].endswith('a')
    assert data[1]['path'].endswith('b')

    path_c = create_file('1234', 'c')
    with assert_exit_code(0):
        head, *data, footer = run_rmlint(
            '--equal', path_a, path_b, path_c,
            use_default_dir=False
        )

    path_d = create_file('diff', 'd')
    with assert_exit_code(1):
        head, *data, footer = run_rmlint(
            '--equal', path_a, path_b, path_d, path_c,
            use_default_dir=False
        )


@with_setup(usual_setup_func, usual_teardown_func)
def test_no_arguments():
    with assert_exit_code(1):
        head, *data, footer = run_rmlint(
            '--equal',
            use_default_dir=False
        )


@with_setup(usual_setup_func, usual_teardown_func)
def test_one_arguments():
    path = create_file('1234', 'a')
    with assert_exit_code(1):
        head, *data, footer = run_rmlint(
            '--equal', path,
            use_default_dir=False
        )

    with assert_exit_code(1):
        head, *data, footer = run_rmlint(
            '--equal', path, "//",
            use_default_dir=False
        )

    with assert_exit_code(1):
        head, *data, footer = run_rmlint(
            '--equal', "//", path,
            use_default_dir=False
        )


@with_setup(usual_setup_func, usual_teardown_func)
def test_equal_directories():
    path_a = os.path.dirname(create_file('xxx', 'dir_a/x'))
    path_b = os.path.dirname(create_file('xxx', 'dir_b/x'))

    with assert_exit_code(0):
        head, *data, footer = run_rmlint(
            '--equal', path_a, path_b,
            use_default_dir=False
        )

    with assert_exit_code(0):
        head, *data, footer = run_rmlint(
            '-p', '--equal', path_a, path_b,
            use_default_dir=False
        )


@with_setup(usual_setup_func, usual_teardown_func)
def test_dir_and_file():
    path_a = os.path.dirname(create_file('xxx', 'dir_a/x'))
    path_b = create_file('xxx', 'x')

    # This should fail since we should not mix directories with files,
    # even if they have the same content.
    with assert_exit_code(1):
        head, *data, footer = run_rmlint(
            '--equal', path_a, path_b,
            use_default_dir=False,
        )


# Regression test for Issue #233
@with_setup(usual_setup_func, usual_teardown_func)
def test_equal_hidden_dirs():
    path_a = os.path.dirname(create_file('xxx', 'dir_a/x'))
    path_b = os.path.dirname(create_file('xxx', '.dir_b/.x'))

    # This should fail since we should not mix directories with files,
    # even if they have the same content.
    with assert_exit_code(0):
        head, *data, footer = run_rmlint(
            '--equal', path_a, path_b,
            use_default_dir=False,
        )


# Regression test for Issue #234
@with_setup(usual_setup_func, usual_teardown_func)
def test_equal_empty_files_or_other_lint():
    path_a = create_file('', 'x')
    path_b = create_file('', 'y')

    # This should fail since we should not mix directories with files,
    # even if they have the same content.
    with assert_exit_code(0):
        head, *data, footer = run_rmlint(
            '--equal', path_a, path_b,
            use_default_dir=False,
        )
