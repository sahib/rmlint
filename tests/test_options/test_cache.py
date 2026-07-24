import hashlib
import os
import subprocess

import pytest

from tests.utils import (
    TESTDIR_NAME,
    check_xattr_capable,
    create_dirs,
    create_file,
    must_read_xattr,
    run_rmlint,
    run_rmlint_once,
)

if skip_msg := check_xattr_capable():
    pytest.skip(skip_msg, allow_module_level=True)


def create_files():
    # Same size, different content.
    create_file('a', '1.a')
    create_file('b', '1.b')

    # Same size, same content.
    create_file('a', '2.a')
    create_file('a', '2.a_')

    # Different content and size
    create_file('a' * 3, '3.a')
    create_file('a' * 4, '3.a_')

    # Size group that will generate ext_cksums for all files
    create_file('b' * 2, '4.a')
    create_file('b' * 2, '4.b')
    create_file('c' * 2, '4.c')
    create_file('c' * 2, '4.d')

    # duplicate_dirs + with --hash-uniques
    create_file('x', 'dir_a/1')
    create_file('x', 'dir_b/1')


@pytest.mark.usefixtures("needs_xattr_fs")
def test_xattr_basic():
    create_files()

    def check(data, write_cache):
        unique = [p['path'] for p in data if p['type'] == 'unique_file']
        dupe_files = [p['path'] for p in data if p['type'] == 'duplicate_file']
        dupe_trees = [p['path'] for p in data if p['type'] == 'duplicate_dir']
        files_in_dupe_dirs = {p['path'] for p in data if p['type'] == 'part_of_directory'}

        def assert_paths(actual, *expected):
            expected = {os.path.join(TESTDIR_NAME, p) for p in expected}
            assert set(actual) == expected

        if write_cache:
            assert len(unique) == 3
            assert_paths(unique, '3.a', '3.a_', '1.b')

            assert len(files_in_dupe_dirs) == 2
            assert_paths(files_in_dupe_dirs, 'dir_a/1', 'dir_b/1')

        assert len(dupe_trees) == 2
        assert_paths(dupe_trees, 'dir_a', 'dir_b')

        assert len(dupe_files) == 7
        assert_paths(dupe_files, '2.a', '2.a_', '1.a', '4.a', '4.b', '4.c', '4.d')


    for _ in range(2):
        for write_cache in True, False:
            if write_cache:
                _, *data, _ = run_rmlint_once('--hash-uniques -D -S pa --xattr-write')
            else:
                _, *data, _ = run_rmlint_once('-D -S pa --xattr-read')

            check(data, write_cache)

        _, *data, _ = run_rmlint_once('-D -S pa --xattr-clear')


BLAKE2B = {
    s: hashlib.blake2b(s).hexdigest().encode("ascii")
    for s in (b'abc', b'def', b'longer')
}


@pytest.mark.usefixtures("needs_xattr_fs")
@pytest.mark.parametrize("extra_opts", ["", "-D"])
def test_xattr_detail(extra_opts):
    xattr_path = create_dirs("xattr_tests")

    # Keep the checksum fixed, if we change the default we don't want to
    # break this test (although I'm sure some tests will break)
    base_options = extra_opts + " -T df -S pa -a blake2b "

    path_1 = os.path.join(xattr_path, "1")
    path_2 = os.path.join(xattr_path, "2")
    path_3 = os.path.join(xattr_path, "3")
    path_4 = os.path.join(xattr_path, "4")

    create_file("abc", path_1)
    create_file("abc", path_2)
    create_file("def", path_3)
    create_file("longer", path_4)

    # ensure path_1 and path_2 have the same mtime, even on a filesystem
    # with sub-second mtime granularity, otherwise xattrs would differ.
    mtime_ns = os.stat(path_1).st_mtime_ns
    os.utime(path_2, ns=(mtime_ns, mtime_ns))

    _, *data, _ = run_rmlint_once(base_options + ' --xattr-write')
    assert len(data) == 2

    xattr_1 = must_read_xattr(path_1)
    xattr_2 = must_read_xattr(path_2)
    xattr_3 = must_read_xattr(path_3)
    xattr_4 = must_read_xattr(path_4)
    assert xattr_1["user.rmlint.blake2b.cksum"] == BLAKE2B[b'abc']
    assert xattr_1 == xattr_2

    # no --hash-unatched given.
    assert not xattr_3

    # no --hash-uniques given.
    assert not xattr_4

    # Run several times with --hash-unmatched.
    for _ in range(10):
        _, *data, _ = run_rmlint_once(base_options + ' --xattr --hash-unmatched')
        # one more due to the size twin
        assert len(data) == 3

        xattr_1 = must_read_xattr(path_1)
        xattr_2 = must_read_xattr(path_2)
        xattr_3 = must_read_xattr(path_3)
        xattr_4 = must_read_xattr(path_4)
        assert xattr_1["user.rmlint.blake2b.cksum"] == BLAKE2B[b'abc']
        assert xattr_1 == xattr_2

        # size-twin with --hash-unmatched.
        xattr_3 = must_read_xattr(path_3)
        assert xattr_3["user.rmlint.blake2b.cksum"] == BLAKE2B[b'def']

        # unique-length file which was not hashed -> does not need to be touched.
        assert not xattr_4

    # Try clearing the attributes:
    _, *data, _ = run_rmlint_once(base_options + '--xattr-clear')
    assert len(data) == 2
    for path in (path_1, path_2, path_3, path_4):
        assert not must_read_xattr(path), path

    # Run several times with --hash-uniques.
    for _ in range(10):
        _, *data, _ = run_rmlint_once(base_options + ' --xattr --hash-uniques')
        # one more due to the 'longer' file
        assert len(data) == 4

        xattr_1 = must_read_xattr(path_1)
        xattr_2 = must_read_xattr(path_2)
        xattr_3 = must_read_xattr(path_3)
        xattr_4 = must_read_xattr(path_4)
        assert xattr_1["user.rmlint.blake2b.cksum"] == BLAKE2B[b'abc']
        assert xattr_1 == xattr_2

        # size-twin with --hash-unmatched.
        xattr_3 = must_read_xattr(path_3)
        assert xattr_3["user.rmlint.blake2b.cksum"] == BLAKE2B[b'def']

        # unique file which was not hashed -> does not need to be touched.
        xattr_4 = must_read_xattr(path_4)
        assert xattr_4["user.rmlint.blake2b.cksum"] == BLAKE2B[b'longer']

    # Try clearing the attributes:
    _, *data, _ = run_rmlint(base_options + '--xattr-clear')
    assert len(data) == 2
    for path in (path_1, path_2, path_3, path_4):
        assert not must_read_xattr(path), path


@pytest.mark.usefixtures("needs_xattr_fs")
def test_treemerge_xattr_hardlink():
    """regression test for GitHub issue #475"""
    create_file('xxx', 'a/x')
    create_file('yyy', 'a/y')
    create_file('xxx', 'b/x')
    create_file('yyy', 'b/y')

    sh_path = os.path.join(TESTDIR_NAME, 'rmlint.sh')
    _, *data, _ = run_rmlint(f'--xattr-write -o sh:{sh_path} -c sh:hardlink')
    assert len(data) == 4

    # run script to hardlink files
    subprocess.check_output([sh_path, '-d'])

    # This used to fail with 'rm_shred_group_free: assertion failed: (self->num_pending == 0)'
    _, *data, _ = run_rmlint('-D --xattr-read')
    assert len(data) == 6


@pytest.mark.usefixtures("needs_xattr_fs")
@pytest.mark.parametrize("clamp", ['-q 1', '-Q 1', '-q 50%', '-Q 50%'])
def test_clamp_xattr_false_negative(clamp):
    create_file('xxx', 'a')
    create_file('yyy', 'b')

    # we used to write xattrs even when clamping is used
    _, *data, _ = run_rmlint('--xattr', clamp)
    assert all(e['type'] == 'unique_file' for e in data)

    create_file('xxx', 'c')

    # the first run after creating 'c' is ok...
    _, *data, _ = run_rmlint('--xattr', force_no_pedantic=True)
    assert len([e for e in data if e['type'] == 'duplicate_file']) == 2  # 'a' matches 'c'

    # but we would get a false negative here, as the xattrs didn't match
    _, *data, _ = run_rmlint('--xattr', force_no_pedantic=True)
    assert len([e for e in data if e['type'] == 'duplicate_file']) == 2  # do they still match?


@pytest.mark.usefixtures("needs_xattr_fs")
@pytest.mark.parametrize("clamp", ['-q 2', '-Q 1', '-q 70%', '-Q 50%'])
def test_clamp_xattr_false_positive(clamp):
    # directories 'a' and 'b' obviously do not match
    # extra files are needed to satisfy preprocessing, which compares file size
    create_file('xxx', '1')
    create_file('xxx', 'a/1')
    create_file('x', '2')
    create_file('x', 'b/2')

    # we used to write xattrs even when clamping is used
    _, *data, _ = run_rmlint('--xattr --size 3', clamp)
    assert len([e for e in data if e['type'] == 'duplicate_file']) == 2  # '1' matches 'a/1'

    # fill in other xattrs
    _, *data, _ = run_rmlint('--xattr', force_no_pedantic=True)
    assert len([e for e in data if e['type'] == 'duplicate_file']) == 4  # '1' matches 'a/1', '2' matches 'b/2'

    # we would get a false positive here, as the xattrs matched
    _, *data, _ = run_rmlint('--xattr -T dd', force_no_pedantic=True)
    assert not any(e['type'] == 'duplicate_dir' for e in data)  # do 'a' and 'b' match?
