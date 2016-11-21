#!/usr/bin/env python3
# encoding: utf-8
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
