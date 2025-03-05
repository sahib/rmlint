#!/usr/bin/env python3
# encoding: utf-8

import os
import subprocess
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


def check_is_reflink_status(status_code, *paths):
    with assert_exit_code(status_code):
        run_rmlint_once(
            '--is-reflink', *paths,
            use_default_dir=False,
            with_json=False,
            verbosity=''
        )


def test_bad_arguments(usual_setup_usual_teardown):
    path_a = create_file('xxx', 'a')
    path_b = create_file('xxx', 'b')
    path_c = create_file('xxx', 'c')
    for paths in [
        (path_a,),
        (path_a, path_b, path_c),
        (path_a, path_a + '.nonexistent')
    ]:
        check_is_reflink_status(1, *paths)


def test_directories(usual_setup_usual_teardown):
    path_a = create_dirs('dir_a')
    path_b = create_dirs('dir_b')
    check_is_reflink_status(3, path_a, path_b)


def test_different_sizes(usual_setup_usual_teardown):
    path_a = create_file('xxx', 'a')
    path_b = create_file('xxxx', 'b')
    check_is_reflink_status(4, path_a, path_b)


def test_same_path(usual_setup_usual_teardown):
    path_a = create_file('xxx', 'a')
    check_is_reflink_status(6, path_a, path_a)


def test_path_double(usual_setup_usual_teardown):
    path_a = create_file('xxx', 'dir/a')
    create_link('dir', 'dir_symlink', symlink=True)
    path_b = os.path.join(TESTDIR_NAME, 'dir_symlink/a')
    check_is_reflink_status(7, path_a, path_b)


def test_hardlinks(usual_setup_usual_teardown):
    path_a = create_file('xxx', 'a')
    path_b = path_a + '_hardlink'
    create_link('a', 'a_hardlink', symlink=False)
    check_is_reflink_status(8, path_a, path_b)


def test_symlink(usual_setup_usual_teardown):
    path_a = create_file('xxx', 'a')
    path_b = create_file('xxx', 'b') + '_symlink'
    create_link('b', 'b_symlink', symlink=True)
    check_is_reflink_status(9, path_a, path_b)
