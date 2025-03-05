#!/usr/bin/env python3
import os

from tests.utils import *


def test_default(usual_setup_usual_teardown):
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

def test_merge_directories_with_ignored_symlinks(usual_setup_usual_teardown):
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

def test_order(usual_setup_usual_teardown):
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


def test_symlink_matches_file(usual_setup_usual_teardown):
    create_file('xxx', 'foo')
    create_file('foo', 'file')
    os.symlink('foo', os.path.join(TESTDIR_NAME, 'link'))

    # --see-symlinks should not generate false positives between unrelated files and links.
    head, *data, footer = run_rmlint()
    assert not data
