#!/usr/bin/env python3
# encoding: utf-8

from nose import with_setup
from nose.tools import make_decorator
from nose.plugins.skip import SkipTest
from contextlib import contextmanager
import psutil

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

def is_btrfs(path):
    parts = psutil.disk_partitions(all=True)

    # iterate up from `path` until mountpoint found
    p = path
    while 1:
        match = next((x for x in parts if x.mountpoint == p), None)
        if(match):
            print("{0} is {1} mounted at {2}".format(path, match.fstype, p))
            return(match.fstype == 'btrfs')

        if(p=='/'):
            # probably should never get here...
            print("no mountpoint found for {0}".format(path))
            return False
        p = os.path.dirname(p)


# decorator for tests dependent on btrfs testdir
def needs_btrfs(test):

    def no_support(*args):
        raise SkipTest("btrfs not supported")

    def not_btrfs(*args):
        raise SkipTest("testdir is not on btrfs filesystem")

    if not has_feature('btrfs-support'):
        return make_decorator(test)(no_support)
    elif not is_btrfs(TESTDIR_NAME):
        return make_decorator(test)(not_btrfs)
    else:
        return test



@needs_btrfs
@with_setup(usual_setup_func, usual_teardown_func)
def test_equal_files():
    path_a = create_file('1234', 'a')
    path_b = create_file('1234', 'b')

    with assert_exit_code(0):
        head, *data, footer = run_rmlint(
            '--btrfs-clone', path_a, path_b,
            use_default_dir=False, with_json=False
        )

    with assert_exit_code(0):
        head, *data, footer = run_rmlint(
            '--btrfs-clone', path_a, '//', path_b,
            use_default_dir=False, with_json=False
        )

@needs_btrfs
@with_setup(usual_setup_func, usual_teardown_func)
def test_different_files():
    path_a = create_file('1234', 'a')
    path_b = create_file('4321', 'b')

    with assert_exit_code(1):
        head, *data, footer = run_rmlint(
            '--btrfs-clone', path_a, path_b,
            use_default_dir=False, with_json=False
        )

@needs_btrfs
@with_setup(usual_setup_func, usual_teardown_func)
def test_bad_arguments():
    path_a = create_file('1234', 'a')
    path_b = create_file('1234', 'b')
    path_c = create_file('1234', 'c')
    for paths in [path_a, ' '.join((path_a, path_b, path_c)), ' '.join((path_a, path_a + ".nonexistent"))]:
        with assert_exit_code(1):
            head, *data, footer = run_rmlint(
                '--btrfs-clone', paths,
                use_default_dir=False, with_json=False
            )


@needs_btrfs
@with_setup(usual_setup_func, usual_teardown_func)
def test_directories():
    path_a = os.path.dirname(create_dirs('dir_a'))
    path_b = os.path.dirname(create_dirs('dir_b'))

    with assert_exit_code(1):
        head, *data, footer = run_rmlint(
            '--btrfs-clone', path_a, path_b,
            use_default_dir=False, with_json=False
        )

@needs_btrfs
@with_setup(usual_setup_func, usual_teardown_func)
def test_clone_works():

    # test files need to be larger than btrfs node size to prevent inline extents
    path_a = create_file('1' * 100000, 'a')
    path_b = create_file('1' * 100000, 'b')

    with assert_exit_code(0):
        head, *data, footer = run_rmlint(
            '--btrfs-clone', path_a, path_b,
            use_default_dir=False, with_json=False
        )

    with assert_exit_code(0):
        head, *data, footer = run_rmlint(
            '--is-clone', path_a, path_b,
            use_default_dir=False, with_json=False
        )
