#!/usr/bin/env python3
# encoding: utf-8
from nose import with_setup
from tests.utils import *


def filter_part_of_directory(data):
    return [e for e in data if e['type'] != 'part_of_directory']


@with_setup(usual_setup_func, usual_teardown_func)
def test_simple():
    create_file('xxx', '.a/1')
    create_file('xxx', '.b/1')
    create_file('xxx', '.1')

    head, *data, footer = run_rmlint('--hidden')

    assert footer['duplicates'] == 2
    assert footer['ignored_folders'] == 0
    assert footer['ignored_files'] == 0
    assert footer['duplicate_sets'] == 1


@with_setup(usual_setup_func, usual_teardown_func)
def test_hidden():
    create_file('xxx', '.a/1')
    create_file('xxx', '.b/1')
    create_file('xxx', '.1')
    head, *data, footer = run_rmlint('--no-hidden')

    assert footer['duplicates'] == 0
    assert footer['ignored_folders'] == 2
    assert footer['ignored_files'] == 3
    assert footer['duplicate_sets'] == 0


@with_setup(usual_setup_func, usual_teardown_func)
def test_explicit():
    create_file('xxx', '.a/1')
    create_file('xxx', '.a/2')
    head, *data, footer = run_rmlint('--no-hidden', dir_suffix='.a')

    assert footer['duplicates'] == 1
    assert footer['ignored_folders'] == 0
    assert footer['ignored_files'] == 0
    assert footer['duplicate_sets'] == 1


@with_setup(usual_setup_func, usual_teardown_func)
def test_partial_hidden():
    create_file('1', 'a/.hidden')
    create_file('1', 'b/.hidden')
    create_file('1', '.hidden')

    head, *data, footer = run_rmlint('--no-hidden')
    assert len(data) == 0

    head, *data, footer = run_rmlint('--partial-hidden')
    assert len(data) == 0

    head, *data, footer = run_rmlint('--hidden')
    assert len(data) == 3
    assert all(p['path'].endswith('.hidden') for p in data)

    head, *data, footer = run_rmlint('--partial-hidden -D -S a')
    data = filter_part_of_directory(data)
    assert len(data) == 2
    assert data[0]['path'].endswith('a')
    assert data[0]['type'] == 'duplicate_dir'
    assert data[1]['path'].endswith('b')

    head, *data, footer = run_rmlint('-D --no-partial-hidden -S a')
    data = filter_part_of_directory(data)
    assert len(data) == 0
