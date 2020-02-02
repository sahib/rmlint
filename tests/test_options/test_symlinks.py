#!/usr/bin/env python3
# encoding: utf-8
from nose import with_setup
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
    assert {e["path"][len(TESTDIR_NAME):] for e in data} == {
        '/a',
        '/b',
    }

@with_setup(usual_setup_func, usual_teardown_func)
def test_order():
    create_file('xxx', 'a/z')
    create_link('a/z', 'a/x', symlink=True)
    create_file('xxx', 'b/z')
    create_link('b/z', 'b/x', symlink=True)
    create_link('b/z', 'b/y', symlink=True)

    head, *data, footer = run_rmlint('-F')
    assert len(data) == 2
    assert data[0]['path'].endswith('z')
    assert data[0]['is_original']
    assert data[1]['path'].endswith('z')

    head, *data, footer = run_rmlint('-f -S a')
    assert sum(p['is_original'] for p in data) == 1

    assert len(data) == 2
    assert data[0]['path'].endswith('z')
    assert data[0]['is_original']

    assert data[1]['path'].endswith('z')
    assert not data[1]['is_original']

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
