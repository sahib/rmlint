#!/usr/bin/env python3
# encoding: utf-8
from nose.plugins.attrib import attr
from nose import with_setup
from tests.utils import *

import os
import subprocess


def set_mtime(path, mtime):
    full_path = os.path.join(TESTDIR_NAME, path)
    subprocess.call(['touch', '-m', '-d', str(mtime), full_path])


@with_setup(usual_setup_func, usual_teardown_func)
def test_consider_mtime():
    create_file('xxx', 'a')
    create_file('xxx', 'b')
    create_file('xxx', 'c')
    create_file('xxx', 'd')

    set_mtime('a', '2004-02-29  16:21:42')
    set_mtime('b', '2004-02-29  16:21:42')
    set_mtime('c', '2004-02-29  16:21:44')
    set_mtime('d', '2004-02-29  16:21:45')

    head, *data, footer = run_rmlint('--mtime-window=-1')
    assert len(data) == 4
    assert footer['total_files'] == 4
    assert footer['total_lint_size'] == 9
    assert footer['duplicates'] == 3
    assert footer['duplicate_sets'] == 1

    head, *data, footer = run_rmlint('--mtime-window=0')
    assert len(data) == 2
    assert footer['total_files'] == 4
    assert footer['total_lint_size'] == 3
    assert footer['duplicates'] == 1
    assert footer['duplicate_sets'] == 1

    head, *data, footer = run_rmlint('--mtime-window=+1')
    assert len(data) == 4
    assert footer['total_files'] == 4
    assert footer['total_lint_size'] == 6  # two originals.
    assert footer['duplicates'] == 2
    assert footer['duplicate_sets'] == 2

    head, *data, footer = run_rmlint('--mtime-window=+2')
    assert len(data) == 4   # '2' also chains up to d from c.
    assert footer['total_files'] == 4
    assert footer['total_lint_size'] == 9
    assert footer['duplicates'] == 3
    assert footer['duplicate_sets'] == 1


@with_setup(usual_setup_func, usual_teardown_func)
def test_consider_mtime_subsecond():
    create_file('xxx', 'a')
    create_file('xxx', 'b')

    set_mtime('a', '2004-02-29  16:21:42.00')
    set_mtime('b', '2004-02-29  16:21:43.99')

    head, *data, footer = run_rmlint('--mtime-window=1.9')
    assert len(data) == 0

    head, *data, footer = run_rmlint('--mtime-window=2.0')
    assert len(data) == 2

    set_mtime('a', '2004-02-29  16:21:42.00')
    set_mtime('b', '2004-02-29  16:21:42.99')

    head, *data, footer = run_rmlint('--mtime-window=0')
    assert len(data) == 0

@with_setup(usual_setup_func, usual_teardown_func)
def test_consider_mtime_fail_by_association():
    create_file('xxx', 'a')
    create_file('yyy', 'b')
    create_file('xxx', 'c')

    set_mtime('a', '2004-02-29  16:21:42')
    set_mtime('b', '2004-02-29  16:21:44')
    set_mtime('c', '2004-02-29  16:21:46')

    head, *data, footer = run_rmlint('--mtime-window=3')

    assert len(data) == 0
    assert footer['total_files'] == 3
    assert footer['total_lint_size'] == 0
    assert footer['duplicates'] == 0
    assert footer['duplicate_sets'] == 0

@with_setup(usual_setup_func, usual_teardown_func)
def test_mtime_and_unmatched_basenames():
    create_file('xxx', 'dir1/a')
    create_file('xxx', 'dir1/c')
    create_file('xxx', 'dir2/a')

    create_file('yyy', 'dir1/b')
    create_file('yyy', 'dir2/b')
    create_file('yyy', 'dir2/c')

    set_mtime('dir1/a', '2004-02-29  16:21:42')
    set_mtime('dir1/c', '2004-02-29  16:21:44')
    set_mtime('dir2/a', '2004-02-29  16:21:48')

    set_mtime('dir1/b', '2004-02-29  16:21:46')
    set_mtime('dir2/b', '2004-02-29  16:21:48')
    set_mtime('dir2/c', '2004-02-29  16:21:50')

    head, *data, footer = run_rmlint('--mtime-window=3 --unmatched-basename -S m')

    assert len(data) == 2
    assert footer['total_files'] == 6
    assert footer['total_lint_size'] == 3
    assert footer['duplicates'] == 1
    assert footer['duplicate_sets'] == 1
