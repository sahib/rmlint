#!/usr/bin/env python3
# encoding: utf-8

import os

from nose import with_setup
from nose.plugins.skip import SkipTest
from parameterized import parameterized
from tests.utils import *


@with_setup(usual_setup_func, usual_teardown_func)
def test_hardlinks_ignored():
    create_file('xxx', 'file_a')
    create_link('file_a', 'file_b')
    create_file('xxx', 'file_z')

    head, *data, footer = run_rmlint('--no-hardlinked -S a')
    assert data[0]["path"].endswith("file_a")
    assert data[0]["is_original"] is True
    assert data[1]["path"].endswith("file_z")
    assert data[1]["is_original"] is False

    head, *data, footer = run_rmlint('--hardlinked -S a')
    assert data[0]["path"].endswith("file_a")
    assert data[0]["is_original"] is True
    assert data[1]["path"].endswith("file_b")
    assert data[1]["is_original"] is False
    assert data[2]["path"].endswith("file_z")
    assert data[2]["is_original"] is False


@parameterized.expand([(False,), (True,)])
@with_setup(usual_setup_func, usual_teardown_func)
def test_keep_hardlinked(replay):
    if replay and not has_feature('replay'):
        raise SkipTest('rmlint built without replay support')

    create_file('xxx', 'file_a')
    create_link('file_a', 'file_b')
    create_file('xxx', 'file_z')
    replay_path = os.path.join(TESTDIR_NAME, 'replay.json')

    replay_args = []
    if replay:
        run_rmlint(with_json='replay.json')
        replay_args += ['--replay', replay_path]

    head, *data, footer = run_rmlint('--keep-hardlinked -S a', *replay_args)
    assert data[0]["path"].endswith("file_a")
    assert data[0]["is_original"] is True
    assert data[1]["path"].endswith("file_b")
    assert data[1]["is_original"] is True
    assert data[2]["path"].endswith("file_z")
    assert data[2]["is_original"] is False

    head, *data, footer = run_rmlint('--keep-hardlinked -S A', *replay_args)
    assert data[0]["path"].endswith("file_z")
    assert data[0]["is_original"] is True
    assert data[1]["path"].endswith("file_b")
    assert data[1]["is_original"] is False
    assert data[2]["path"].endswith("file_a")
    assert data[2]["is_original"] is False


@parameterized.expand([(False,), (True,)])
@with_setup(usual_setup_func, usual_teardown_func)
def test_keep_hardlinked_elim(replay):
    if replay and not has_feature('replay'):
        raise SkipTest('rmlint built without replay support')

    create_file('xxx', 'file_a')
    create_link('file_a', 'file_b')
    replay_path = os.path.join(TESTDIR_NAME, 'replay.json')

    replay_args = []
    if replay:
        run_rmlint(with_json='replay.json')
        replay_args += ['--replay', replay_path]

    head, *data, foot = run_rmlint('--keep-hardlinked', *replay_args)
    assert not data
    assert foot['duplicates'] == 0
    assert foot['duplicate_sets'] == 0


@with_setup(usual_setup_func, usual_teardown_func)
def test_keep_hardlinks_multiple_originals():
    create_file('xxx', 'a/file_a')
    create_file('xxx', 'a/file_y')
    create_dirs('b')
    create_link('a/file_a', 'b/file_b')
    create_link('a/file_y', 'b/file_z')

    search_paths = TESTDIR_NAME + '/b // ' + TESTDIR_NAME + '/a'

    head, *data, footer = run_rmlint('--no-hardlinked -S a ' + search_paths, use_default_dir=False)
    # hardlinks file_b and file_z should be ignored
    assert len(data)==2
    assert data[0]["path"].endswith("file_a")
    assert data[0]["is_original"] is True
    assert data[1]["path"].endswith("file_y")
    assert data[1]["is_original"] is False

    head, *data, footer = run_rmlint('--hardlinked -k -m -S a ' + search_paths, use_default_dir=False)
    # files in folder a should both be originals because tagged
    assert len(data)==4
    assert data[0]["path"].endswith("file_a")
    assert data[0]["is_original"] is True
    assert data[1]["path"].endswith("file_y")
    assert data[1]["is_original"] is True
    assert data[2]["path"].endswith("file_b")
    assert data[2]["is_original"] is False
    assert data[3]["path"].endswith("file_z")
    assert data[3]["is_original"] is False

    head, *data, footer = run_rmlint(
        '--keep-hardlinked -k -m -S a -c json:unique ' + search_paths,
        use_default_dir=False,
    )
    # files in folder a are tagged so should both be preserved;
    # files in folder b are hardlinks of the two originals so should also be preserved
    # therefore all files are originals and so don't get reported
    assert len(data) == 4  # unique files
    assert all(e['type'] == 'unique_file' for e in data)
    assert footer['duplicate_sets'] == 0


@with_setup(usual_setup_func, usual_teardown_func)
def test_merge_directories():
    create_file('xxx', 'a/x')
    create_file('xxx', 'b/x')
    create_link('a/x', 'a/z')
    create_link('b/x', 'b/z')

    # --keep-hardlinked should apply to files emitted by treemerge
    head, *data, foot = run_rmlint('-D -S a --keep-hardlinked')
    data = [e for e in data if e['type'] != 'part_of_directory']
    assert len(data) == 2
    assert data[0]['path'].endswith('/a')
    assert data[1]['path'].endswith('/b')
