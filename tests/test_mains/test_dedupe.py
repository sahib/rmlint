#!/usr/bin/env python3
# encoding: utf-8

from nose import with_setup
from nose.tools import make_decorator
from nose.plugins.skip import SkipTest
from contextlib import contextmanager
import psutil

from tests.utils import *

REFLINK_CAPABLE_FILESYSTEMS = {'btrfs', 'xfs', 'ocfs2'}

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


def up(path):
    while path:
        yield path
        if path == "/":
            break
        path = os.path.dirname(path)

def is_on_reflink_fs(path):
    parts = psutil.disk_partitions(all=True)

    # iterate up from `path` until mountpoint found
    for up_path in up(path):
        for part in parts:
            if up_path == part.mountpoint:
                print("{0} is {1} mounted at {2}".format(path, part.fstype, part.mountpoint))
                return (part.fstype in REFLINK_CAPABLE_FILESYSTEMS)

    print("No mountpoint found for {0}", path)
    return False


# decorator for tests dependent on reflink-capable testdir
def needs_reflink_fs(test):
    def no_support(*args):
        raise SkipTest("btrfs not supported")

    def not_reflink_fs(*args):
        raise SkipTest("testdir is not on reflink-capable filesystem")

    if not has_feature('btrfs-support'):
        return make_decorator(test)(no_support)
    elif not is_on_reflink_fs(TESTDIR_NAME):
        return make_decorator(test)(not_reflink_fs)
    else:
        return test


@needs_reflink_fs
@with_setup(usual_setup_func, usual_teardown_func)
def test_equal_files():
    path_a = create_file('1234', 'a')
    path_b = create_file('1234', 'b')

    with assert_exit_code(0):
        run_rmlint(
            '--dedupe',
            path_a, path_b,
            use_default_dir=False,
            with_json=False,
            verbosity="")

    with assert_exit_code(0):
        run_rmlint(
            '--dedupe',
            path_a, '//', path_b,
            use_default_dir=False,
            with_json=False)


@needs_reflink_fs
@with_setup(usual_setup_func, usual_teardown_func)
def test_different_files():
    path_a = create_file('1234', 'a')
    path_b = create_file('4321', 'b')

    with assert_exit_code(1):
        run_rmlint(
            '--dedupe',
            path_a, path_b,
            use_default_dir=False,
            with_json=False,
            verbosity="")


@needs_reflink_fs
@with_setup(usual_setup_func, usual_teardown_func)
def test_bad_arguments():
    path_a = create_file('1234', 'a')
    path_b = create_file('1234', 'b')
    path_c = create_file('1234', 'c')
    for paths in [
            path_a,
            ' '.join((path_a, path_b, path_c)),
            ' '.join((path_a, path_a + ".nonexistent"))
    ]:
        with assert_exit_code(1):
            run_rmlint(
                '--dedupe',
                paths,
                use_default_dir=False,
                with_json=False,
                verbosity="")


@needs_reflink_fs
@with_setup(usual_setup_func, usual_teardown_func)
def test_directories():
    path_a = os.path.dirname(create_dirs('dir_a'))
    path_b = os.path.dirname(create_dirs('dir_b'))

    with assert_exit_code(1):
        run_rmlint(
            '--dedupe',
            path_a, path_b,
            use_default_dir=False,
            with_json=False,
            verbosity="")


@needs_reflink_fs
@with_setup(usual_setup_func, usual_teardown_func)
def test_dedupe_works():

    # test files need to be larger than btrfs node size to prevent inline extents
    path_a = create_file('1' * 100000, 'a')
    path_b = create_file('1' * 100000, 'b')

    with assert_exit_code(1):
        run_rmlint(
            '--is-reflink', path_a, path_b,
            use_default_dir=False,
            with_json=False,
            verbosity=""
        )

    with assert_exit_code(0):
        run_rmlint(
            '--dedupe', path_a, path_b,
            use_default_dir=False,
            with_json=False,
            verbosity=""
        )

    with assert_exit_code(0):
        run_rmlint(
            '--is-reflink', path_a, path_b,
            use_default_dir=False,
            with_json=False,
            verbosity=""
        )
