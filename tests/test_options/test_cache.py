#!/usr/bin/env python3
# encoding: utf-8
from nose import with_setup
from tests.utils import *
from parameterized import parameterized


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

    # duplicate_dirs + with --write_unfinished
    create_file('x', 'dir_a/1')
    create_file('x', 'dir_b/1')


def check(data, write_cache):
    unfinished = [p['path'] for p in data if p['type'] == 'unique_file']
    dupe_files = [p['path'] for p in data if p['type'] == 'duplicate_file']
    dupe_trees = [p['path'] for p in data if p['type'] == 'duplicate_dir']

    path_in = lambda name, paths: os.path.join(TESTDIR_NAME, name) in paths

    if write_cache:
        assert len(unfinished) == 3
        assert path_in('1.b', unfinished)
        assert path_in('dir_a/1', unfinished)
        assert path_in('dir_b/1', unfinished)

    assert len(dupe_trees) == 2
    assert path_in('dir_a', dupe_trees)
    assert path_in('dir_b', dupe_trees)

    assert len(dupe_files) == 7
    assert path_in('2.a', dupe_files)
    assert path_in('2.a_', dupe_files)
    assert path_in('1.a', dupe_files)
    assert path_in('4.a', dupe_files)
    assert path_in('4.b', dupe_files)
    assert path_in('4.c', dupe_files)
    assert path_in('4.d', dupe_files)


@with_setup(usual_setup_func, usual_teardown_func)
def test_xattr_basic():
    create_files()

    for _ in range(2):
        for write_cache in True, False:
            if write_cache:
                head, *data, footer = run_rmlint('-U -D -S pa --xattr-write')
            else:
                head, *data, footer = run_rmlint('-D -S pa --xattr-read')

            check(data, write_cache)

        head, *data, footer = run_rmlint('-D -S pa --xattr-clear')


@parameterized([("", ), ("-D", )])
@with_setup(usual_setup_func, usual_teardown_func)
def test_xattr_detail(extra_opts):
    if not runs_as_root():
        # This tests need a ext4 fs which is created during the test.
        # The mount step sadly needs root privileges.
        return

    with create_special_fs("this-is-not-tmpfs") as ext4_path:
        # Keep the checksum fixed, if we change the default we don't want to
        # break this test (although I'm sure some tests will break)
        base_options = extra_opts + " -T df -S pa -a blake2b "

        path_1 = os.path.join(ext4_path, "1")
        path_2 = os.path.join(ext4_path, "2")
        path_3 = os.path.join(ext4_path, "3")
        path_4 = os.path.join(ext4_path, "4")

        create_file("abc", path_1)
        create_file("abc", path_2)
        create_file("def", path_3)
        create_file("longer", path_4)

        head, *data, footer = run_rmlint(base_options + ' --xattr-write')
        assert len(data) == 2

        xattr_1 = must_read_xattr(path_1)
        xattr_2 = must_read_xattr(path_2)
        xattr_3 = must_read_xattr(path_3)
        assert xattr_1["user.rmlint.blake2b.cksum"] == \
                b'ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d17d87c5392aab792dc252d5de4533cc9518d38aa8dbf1925ab92386edd4009923\x00'
        assert xattr_1 == xattr_2

        # no --write-unfinished given.
        assert xattr_3 == {}

        # Repeating the caching option should have no effect on the output.
        for _ in range(10):
            head, *data, footer = run_rmlint(base_options + ' --xattr')
            # one more due to the unique_file
            assert len(data) == 3

            xattr_1 = must_read_xattr(path_1)
            xattr_2 = must_read_xattr(path_2)
            xattr_3 = must_read_xattr(path_3)
            assert xattr_1["user.rmlint.blake2b.cksum"] == \
                    b'ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d17d87c5392aab792dc252d5de4533cc9518d38aa8dbf1925ab92386edd4009923\x00'
            assert xattr_1 == xattr_2

            # --write-unfinished will also write the unfinished one.
            xattr_3 = must_read_xattr(path_3)
            assert xattr_3["user.rmlint.blake2b.cksum"] == \
                    b'36badf2227521b798b78d1bd43c62520a35b9b541547ff223f35f74b1168da2cd3c8d102aaee1a0cc217b601258d80151067cdee3a6352517b8fc7f7106902d3\x00'

            # unique file which was not hashed -> does not need to be touched.
            xattr_4 = must_read_xattr(path_4)
            assert xattr_4 == {}

        # Try clearing the attributes:
        head, *data, footer = run_rmlint(base_options + '--xattr-clear')
        assert len(data) == 2
        assert must_read_xattr(path_1) == {}
        assert must_read_xattr(path_2) == {}
        assert must_read_xattr(path_3) == {}
        assert must_read_xattr(path_4) == {}
