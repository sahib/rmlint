#!/usr/bin/env python3
# encoding: utf-8
from nose import with_setup
from tests.utils import *


@with_setup(usual_setup_func, usual_teardown_func)
def test_simple():
    create_file('xxx', '1/a')
    create_file('xxx', '2/a')
    create_file('xxx', 'a')

    head, *data, footer = run_rmlint('-pp -D --rank-by A')

    assert 2 == sum(find['type'] == 'duplicate_dir' for find in data)

    # One original, one dupe
    assert 1 == sum(find['type'] == 'duplicate_file' for find in data if find['is_original'])
    assert 1 == sum(find['type'] == 'duplicate_file' for find in data if not find['is_original'])
    assert data[0]['size'] == 3

    # -S A should sort in reverse lexigraphic order.
    assert data[0]['is_original']
    assert not data[1]['is_original']
    assert data[0]['path'].endswith('2')
    assert data[1]['path'].endswith('1')


@with_setup(usual_setup_func, usual_teardown_func)
def test_diff():
    create_file('xxx', '1/a')
    create_file('xxx', '2/a')
    create_file('xxx', '3/a')
    create_file('yyy', '3/b')
    head, *data, footer = run_rmlint('-pp -D --rank-by A')

    assert 2 == sum(find['type'] == 'duplicate_dir' for find in data)
    assert data[0]['size'] == 3

    # -S A should sort in reverse lexigraphic order.
    assert data[0]['is_original']
    assert not data[1]['is_original']
    assert data[0]['path'].endswith('2')
    assert data[1]['path'].endswith('1')


@with_setup(usual_setup_func, usual_teardown_func)
def test_same_but_not_dupe():
    create_file('xxx', '1/a')
    create_file('xxx', '2/a')
    create_file('xxx', '2/b')
    head, *data, footer = run_rmlint('-pp -D --rank-by A')

    # No duplicate dirs, but 3 duplicate files should be found.
    assert 0 == sum(find['type'] == 'duplicate_dir' for find in data)
    assert 3 == sum(find['type'] == 'duplicate_file' for find in data)

@with_setup(usual_setup_func, usual_teardown_func)
def test_hardlinks():
    create_file('xxx', '1/a')
    create_link('1/a', '1/link1')
    create_link('1/a', '1/link2')
    create_file('xxx', '2/a')
    create_link('2/a', '2/link1')
    create_link('2/a', '2/link2')

    head, *data, footer = run_rmlint('-pp -D -l -S a')
    assert len(data) is 5
    assert data[0]['type'] == 'duplicate_dir'
    assert data[0]['path'].endswith('1')
    assert data[1]['type'] == 'duplicate_dir'
    assert data[1]['path'].endswith('2')

    # Hardlink duplicates:
    assert data[2]['type'] == 'duplicate_file'
    assert data[2]['path'].endswith('1/a')
    assert data[2]['is_original']
    assert data[3]['type'] == 'duplicate_file'
    assert data[3]['path'].endswith('1/link1')
    assert not data[3]['is_original']
    assert data[4]['type'] == 'duplicate_file'
    assert data[4]['path'].endswith('1/link2')
    assert not data[4]['is_original']

    head, *data, footer = run_rmlint('-D -S a -L')
    assert len(data) is 2
    assert data[0]['type'] == 'duplicate_file'
    assert data[0]['path'].endswith('a')
    assert data[1]['type'] == 'duplicate_file'
    assert data[1]['path'].endswith('a')


@with_setup(usual_setup_func, usual_teardown_func)
def test_deep_simple():
    create_file('xxx', 'deep/a/b/c/d/1')
    create_file('xxx', 'deep/e/f/g/h/1')
    head, *data, footer = run_rmlint('-D -S a')

    assert data[0]['path'].endswith('deep/a')
    assert data[1]['path'].endswith('deep/e')
    assert int(data[0]['checksum'], 16) > 0
    assert int(data[1]['checksum'], 16) > 0
    assert len(data) == 2


@with_setup(usual_setup_func, usual_teardown_func)
def test_deep_simple():
    create_file('xxx', 'd/a/1')
    create_file('xxx', 'd/b/empty')
    create_file('xxx', 'd/a/1')
    create_file('xxx', 'd/b/empty')
    head, *data, footer = run_rmlint('-pp -D -S a')

    assert data[0]['path'].endswith('d/a')
    assert data[1]['path'].endswith('d/b')
    assert len(data) == 2


def create_nested(root, letters):
    summed = []
    for letter in letters:
        summed.append(letter)
        path = os.path.join(*([root] + summed + ['1']))
        create_file('xxx', path)


@with_setup(usual_setup_func, usual_teardown_func)
def test_deep_full():
    create_nested('deep', 'abcd')
    create_nested('deep', 'efgh')

    # subprocess.call('tree ' + TESTDIR_NAME, shell=True)
    # subprocess.call('./rmlint -p -S a -D ' + TESTDIR_NAME, shell=True)
    head, *data, footer = run_rmlint('-pp -D -S a')

    assert len(data) == 6

    assert data[0]['path'].endswith('deep/a')
    assert data[0]['type'] == 'duplicate_dir'
    assert data[0]['is_original']
    assert data[1]['path'].endswith('deep/e')
    assert not data[1]['is_original']
    assert data[1]['type'] == 'duplicate_dir'

    for idx, ending in enumerate(['a/b/c/d/1', 'a/b/c/1', 'a/b/1', 'a/1']):
        assert data[idx + 2]['path'].endswith(ending)
        assert data[idx + 2]['type'] == 'duplicate_file'
        assert data[idx + 2]['is_original'] == (idx is 0)


@with_setup(usual_setup_func, usual_teardown_func)
def test_deep_full_twice():
    create_nested('deep_a', 'abcd')
    create_nested('deep_a', 'efgh')
    create_nested('deep_b', 'abcd')
    create_nested('deep_b', 'efgh')

    # subprocess.call('tree ' + TESTDIR_NAME, shell=True)
    # subprocess.call('./rmlint -S a -D ' + TESTDIR_NAME + '/deep_a ' + TESTDIR_NAME + '/deep_b', shell=True)

    head, *data, footer = run_rmlint(
        '-D -S a {t}/deep_a {t}/deep_b'.format(
            t=TESTDIR_NAME
        ),
        use_default_dir=False
    )

    assert len(data) == 8

    assert data[0]['path'].endswith('deep_a')
    assert data[0]['type'] == 'duplicate_dir'
    assert data[0]['is_original']
    assert data[1]['path'].endswith('deep_b')
    assert data[1]['is_original'] == False
    assert data[1]['type'] == 'duplicate_dir'

    assert data[2]['path'].endswith('deep_a/a')
    assert data[2]['type'] == 'duplicate_dir'
    assert data[2]['is_original']
    assert data[3]['path'].endswith('deep_a/e')
    assert data[3]['is_original'] == False
    assert data[3]['type'] == 'duplicate_dir'

    for idx, ending in enumerate(['a/b/c/d/1', 'a/b/c/1', 'a/b/1', 'a/1']):
        assert data[idx + 4]['path'].endswith(ending)
        assert data[idx + 4]['type'] == 'duplicate_file'
        assert data[idx + 4]['is_original'] == (idx is 0)

    assert data[0]['path'].endswith('deep_a')
    assert data[0]['is_original']
    assert data[1]['path'].endswith('deep_b')
    assert not data[1]['is_original']
    assert data[2]['path'].endswith('deep_a/a')
    assert data[2]['is_original']
    assert data[3]['path'].endswith('deep_a/e')
    assert not data[3]['is_original']


@with_setup(usual_setup_func, usual_teardown_func)
def test_symlinks():
    create_file('xxx', 'a/z')
    create_link('a/z', 'a/x', symlink=True)
    create_file('xxx', 'b/z')
    create_link('b/z', 'b/x', symlink=True)

    head, *data, footer = run_rmlint('-pp -D -S a -F')

    assert len(data) == 2
    assert data[0]['path'].endswith('z')
    assert data[0]['is_original']
    assert data[1]['path'].endswith('z')
    assert not data[1]['is_original']

    head, *data, footer = run_rmlint('-pp -D -S a -f')

    assert len(data) == 4
    assert data[0]['path'].endswith('/a')
    assert data[0]['is_original']
    assert data[1]['path'].endswith('/b')
    assert not data[1]['is_original']

    # z must come first, since it's the physical real file
    assert data[2]['path'].endswith('/a/z')
    assert data[2]['is_original']

    # x is a duplicate of z when following links
    assert data[3]['path'].endswith('/a/x')
    assert not data[3]['is_original']


def mount_bind_teardown_func():
    if runs_as_root():
        subprocess.call(
            'umount {dst}'.format(
                dst=os.path.join(TESTDIR_NAME, 'a/b')
            ),
            shell=True
        )

    usual_teardown_func()


@with_setup(usual_setup_func, mount_bind_teardown_func)
def test_mount_binds():
    if not runs_as_root():
        return

    create_file('xxx', 'a/b/1')
    create_file('xxx', 'c/2')

    subprocess.call(
        'mount --rbind {src} {dst}'.format(
            src=TESTDIR_NAME,
            dst=os.path.join(TESTDIR_NAME, 'a/b')
        ),
        shell=True
    )
    create_file('xxx', 'a/3')

    head, *data, footer = run_rmlint('-S a')
    assert data[0]['path'].endswith('c/2')
    assert data[1]['path'].endswith('a/3')
    assert len(data) == 2


@with_setup(usual_setup_func, usual_teardown_func)
def test_keepall_tagged():
    # Test for Issue #141:
    # https://github.com/sahib/rmlint/issues/141
    #
    # Make sure -k protects duplicate directories too,
    # when they're in a pref'd path.

    create_file('test', 'origs/folder/subfolder/file')
    create_file('test', 'origs/samefolder/subfolder/file')
    create_file('test', 'dups/folder/subfolder/file')
    create_file('test', 'dups/samefolder/subfolder/file')

    for untagged_path in [os.path.join(TESTDIR_NAME, 'origs'), TESTDIR_NAME]:
        for options in ['-D -S a -k -m {d} // {o}', '-D -S a -k {d} // {o}']:
            head, *data, footer = run_rmlint(options.format(
                d=untagged_path,
                o=os.path.join(TESTDIR_NAME, 'origs')
            ))

            assert len(data) == 4
            assert footer['total_files'] == 4
            assert footer['duplicates'] == 2
            assert footer['duplicate_sets'] == 1

            assert data[0]['path'].endswith('origs')
            assert data[0]['is_original']

            assert data[1]['path'].endswith('dups')
            assert not data[1]['is_original']

            assert data[2]['path'].endswith('origs/folder')
            assert data[2]['is_original']

            assert data[3]['path'].endswith('origs/samefolder')
            assert data[3]['is_original']


@with_setup(usual_setup_func, usual_teardown_func)
def test_keepall_untagged():
    create_file('test', 'origs/folder/subfolder/file')
    create_file('test', 'origs/samefolder/subfolder/file')
    create_file('test', 'dups/folder/subfolder/file')
    create_file('test', 'dups/samefolder/subfolder/file')

    for untagged_path in [os.path.join(TESTDIR_NAME, 'origs'), TESTDIR_NAME]:
        for options in ['-D -S a -K -M {d} // {o}', '-D -S a -K {d} // {o}']:
            head, *data, footer = run_rmlint(options.format(
                d=untagged_path,
                o=os.path.join(TESTDIR_NAME, 'origs')
            ))

            assert len(data) == 4
            assert footer['total_files'] == 4
            assert footer['duplicates'] == 2
            assert footer['duplicate_sets'] == 1

            assert data[0]['path'].endswith('dups')
            assert data[0]['is_original']

            assert data[1]['path'].endswith('origs')
            assert not data[1]['is_original']

            assert data[2]['path'].endswith('dups/folder')
            assert data[2]['is_original']

            assert data[3]['path'].endswith('dups/samefolder')
            assert data[3]['is_original']
