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
def assert_exit_code(expected, fussy=False):
    """
    Assert that the with block yields a subprocess.CalledProcessError
    with a certain return code. If nothing is thrown, status_code
    is required to be 0 to survive the test.
    If the flag `fussy` is True then suprocess must return the
    exact same error as expected.  If False then any error code
    is ok, provided the expected result is non-zero.
    """
    try:
        yield
    except subprocess.CalledProcessError as exc:
        if (fussy) :
            assert exc.returncode == expected
        else:
            assert exc.returncode != 0 and expected != 0
    else:
        # No exception? status_code should be fine.
        assert expected == 0


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
                print("{0} is {1} mounted at {2}".format(
                    path, part.fstype, part.mountpoint))
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
    # test files need to be larger than btrfs node size to prevent inline extents
    path_a = create_file('1234' * 4096, 'a')
    path_b = create_file('1234' * 4096, 'b')

    with assert_exit_code(0):
        run_rmlint_once(
            '--dedupe',
            path_a, path_b,
            use_default_dir=False,
            with_json=False,
            verbosity="")


@needs_reflink_fs
@with_setup(usual_setup_func, usual_teardown_func)
def test_hardlinks():
    # test files need to be larger than btrfs node size to prevent inline extents
    path_a = create_file('1234' * 4096, 'a')
    path_b = path_a + '_hardlink'
    create_link('a', 'a_hardlink', symlink=False)

    with assert_exit_code(0):
        run_rmlint_once(
            '--dedupe',
            path_a, path_b,
            use_default_dir=False,
            with_json=False,
            verbosity="")


@needs_reflink_fs
@with_setup(usual_setup_func, usual_teardown_func)
def test_different_files():
    # test files need to be larger than btrfs node size to prevent inline extents
    path_a = create_file('1234' * 4096, 'a')
    path_b = create_file('4321' * 4096, 'b')

    with assert_exit_code(1):
        run_rmlint_once(
            '--dedupe',
            path_a, path_b,
            use_default_dir=False,
            with_json=False,
            verbosity="")


@needs_reflink_fs
@with_setup(usual_setup_func, usual_teardown_func)
def test_bad_arguments():
    # test files need to be larger than btrfs node size to prevent inline extents
    path_a = create_file('1234' * 4096, 'a')
    path_b = create_file('1234' * 4096, 'b')
    path_c = create_file('1234' * 4096, 'c')
    for paths in [
            path_a,
            ' '.join((path_a, path_b, path_c)),
            ' '.join((path_a, path_a + ".nonexistent"))
    ]:
        with assert_exit_code(1):
            run_rmlint_once(
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
        run_rmlint_once(
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

    # confirm that files are not reflinks
    with assert_exit_code(11):
        run_rmlint_once(
            '--is-reflink', path_a, path_b,
            use_default_dir=False,
            with_json=False,
            verbosity=""
        )

    # reflink our files
    with assert_exit_code(0):
        run_rmlint_once(
            '--dedupe', path_a, path_b,
            use_default_dir=False,
            with_json=False,
            verbosity=""
        )

    # confirm that they are now reflinks
    with assert_exit_code(0):
        run_rmlint_once(
            '--is-reflink', path_a, path_b,
            use_default_dir=False,
            with_json=False,
            verbosity=""
        )

# count the number of line in a file which start with patterns[]


def pattern_count(path, patterns):
    counts = [0] * len(patterns)
    with open(path, 'r') as f:
        for line in f:
            for i, pattern in enumerate(patterns):
                if line.startswith(pattern):
                    counts[i] += 1
    return counts


@needs_reflink_fs
@with_setup(usual_setup_func, usual_teardown_func)
def test_clone_handler():
    # test files need to be larger than btrfs node size to prevent inline extents
    path_a = create_file('1' * 100000, 'a')
    path_b = create_file('1' * 100000, 'b')

    sh_path = os.path.join(TESTDIR_NAME, 'rmlint.sh')

    # generate rmlint.sh and check that it correctly selects files for cloning
    with assert_exit_code(0):
        run_rmlint_once(
            '-S a -o sh:{p} -c sh:clone'.format(p=sh_path),
            path_a, path_b,
            use_default_dir=False,
            with_json=False
        )

    # parse output file for expected clone command
    patterns = [
        "clone         '",
        "skip_reflink  '"]
    counts = pattern_count(sh_path, patterns)
    print(counts)
    assert counts[0] == 1
    assert counts[1] == 0

    # now reflink the two files and check again
    with assert_exit_code(0):
        run_rmlint_once(
            '--dedupe', path_a, path_b,
            use_default_dir=False,
            with_json=False,
            verbosity=""
        )
    with assert_exit_code(0):
        run_rmlint_once(
            '-S a -o sh:{p} -c sh:clone'.format(p=sh_path),
            path_a, path_b,
            use_default_dir=False,
            with_json=False
        )

    counts = pattern_count(sh_path, patterns)
    assert counts[0] == 0
    assert counts[1] == 1
