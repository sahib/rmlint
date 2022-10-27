#!/usr/bin/env python3
# encoding: utf-8

import os
import shlex

from nose import with_setup
from parameterized import parameterized
from tests.utils import *


@with_setup(usual_setup_func, usual_teardown_func)
def test_default():
    # --see-symlinks should be on by default.
    create_file('xxx', 'a/z')
    create_link('a/z', 'a/x', symlink=True)
    create_file('xxx', 'b/z')
    create_link('b/z', 'b/x', symlink=True)
    create_link('b/z', 'b/y', symlink=True)

    head, *data, footer = run_rmlint()
    assert {e["path"][len(TESTDIR_NAME):] for e in data} == {
        '/b/x',
        '/b/y',
        '/b/z',
        '/a/z',
    }

@with_setup(usual_setup_func, usual_teardown_func)
def test_merge_directories_with_ignored_symlinks():
    # Badlinks should not forbid finding duplicate directories
    # when being filtered out during traversing with -T dd,df.
    create_file('xxx', 'a/z')
    create_link('bogus', 'a/link', symlink=True)
    create_file('xxx', 'b/z')
    create_link('bogus', 'b/link', symlink=True)

    head, *data, footer = run_rmlint('-T df,dd')
    assert {e["path"][len(TESTDIR_NAME):] for e in data if e["type"] == "duplicate_dir"} == {
        '/a',
        '/b',
    }


def get_file_names(data):
    return {e['path'][len(TESTDIR_NAME):] for e in data}


def get_orig_flags(data):
    return [int(p['is_original']) for p in data]


@parameterized.expand([('',), ('--no-keep-symlinks',), ('--no-keep-symlinks --keep-hardlinked',)])
@with_setup(usual_setup_func, usual_teardown_func)
def test_order1(nks_opt):
    create_file('xxx', 'a/z')
    create_link('a/z', 'a/x', symlink=True)
    create_file('xxx', 'b/z')
    create_link('b/z', 'b/x', symlink=True)
    create_link('b/z', 'b/y', symlink=True)

    head, *data, footer = run_rmlint('-F')
    assert len(data) == 2
    assert get_file_names(data) == {'/a/z', '/b/z'}
    assert get_orig_flags(data) == [1, 0]

    head, *data, footer = run_rmlint('-f -S a', nks_opt)
    assert len(data) == 5

    assert get_orig_flags(data) == [1, 0] + 3 * [0 if nks_opt else 1]
    assert get_file_names(data[0:2]) == {'/a/z', '/b/z'}
    assert get_file_names(data[2:4]) == {'/a/x', '/b/x'}
    assert get_file_names(data[4:]) == {'/b/y'}


@with_setup(usual_setup_func, usual_teardown_func)
def test_order2():
    create_file('xxx', 'a/z')
    create_link('a/z', 'a/x', symlink=True)
    create_file('xxx', 'b/z')
    create_link('b/z', 'b/x', symlink=True)
    create_link('b/z', 'b/y', symlink=True)

    head, *data, footer = run_rmlint('-@ -S a')
    assert sum(p['is_original'] for p in data) == 2
    assert len(data) == 4

    if data[0]['path'].endswith('x'):
        sym_idx, file_idx = 0, 2
    else:
        sym_idx, file_idx = 2, 0

    # Same symlink -> dupe
    assert data[sym_idx]['path'].endswith('x')
    assert data[sym_idx]['is_original']
    assert data[sym_idx + 1]['path'].endswith('y')

    # Same file
    assert data[file_idx]['path'].endswith('z')
    assert data[file_idx]['is_original']
    assert data[file_idx + 1]['path'].endswith('z')


@with_setup(usual_setup_func, usual_teardown_func)
def test_keep_symlinks():
    create_file('xxx', 'a')
    create_link('a', 'l', symlink=True)

    # --keep-symlinks should ignore lone files with symlinks
    head, *data, foot = run_rmlint('-f')
    assert not data
    assert foot['duplicates'] == 0
    assert foot['duplicate_sets'] == 0

    # --keep-symlinks should mark all symlinks as original
    create_file('xxx', 'b')
    head, *data, foot = run_rmlint('-f -S A')
    assert len(data) == 3
    assert get_file_names(data) == {'/b', '/a', '/l'}
    assert get_orig_flags(data) == [1, 0, 1]
    assert foot['duplicates'] == 1
    assert foot['duplicate_sets'] == 1


@with_setup(usual_setup_func, usual_teardown_func)
def test_keep_symlinks_merge_directories():
    create_file('xxx', 'a/x')
    create_file('xxx', 'b/x')
    create_link('a/x', 'a/z', symlink=True)
    create_link('b/x', 'b/z', symlink=True)

    # --keep-symlinks should apply to files emitted by treemerge
    head, *data, foot = run_rmlint('-D -S a -f')
    data = [e for e in data if e['type'] != 'part_of_directory']
    assert len(data) == 2
    assert data[0]['path'].endswith('/a')
    assert data[1]['path'].endswith('/b')


@with_setup(usual_setup_func, usual_teardown_func)
def test_replay_followlinks_differs():
    create_file('xxx', 'a')
    create_file('xxx', 'b')
    replay_path = os.path.join(TESTDIR_NAME, 'replay.json')

    head, *data, foot = run_rmlint('-f', with_json='replay.json')
    assert len(data) == 2
    with assert_exit_code(1):
        run_rmlint('--replay', shlex.quote(replay_path))

    head, *data, foot = run_rmlint(with_json='replay.json')
    assert len(data) == 2
    with assert_exit_code(1):
        run_rmlint('-f --replay', shlex.quote(replay_path))


@with_setup(usual_setup_func, usual_teardown_func)
def test_replay_badlink_differs():
    path_a = create_file('xxx', 'a')
    create_link('a', 'b', symlink=True)
    replay_path = os.path.join(TESTDIR_NAME, 'replay.json')

    # run with good symlink
    head, *data, foot = run_rmlint('-c json:unique', with_json='replay.json')
    assert len(data) == 2
    assert data[0]['type'] == 'unique_file'
    assert data[1]['type'] == 'unique_file'

    # break the link
    os.unlink(path_a)

    head, *data, foot = run_rmlint('--replay', shlex.quote(replay_path))
    assert len(data) == 1
    assert data[0]['type'] == 'badlink'  # previously good link is now broken

    # run with bad symlink
    head, *data, foot = run_rmlint(with_json='replay.json')
    assert len(data) == 1
    assert data[0]['type'] == 'badlink'

    # fix the link
    create_file('xxx', 'a')

    head, *data, foot = run_rmlint('--replay', shlex.quote(replay_path))
    assert not data  # previously broken link is ignored
