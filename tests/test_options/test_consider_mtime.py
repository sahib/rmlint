#!/usr/bin/env python3
# encoding: utf-8
from nose import with_setup
from tests.utils import *

import os
import subprocess


@with_setup(usual_setup_func, usual_teardown_func)
def test_consider_mtime():
    create_file('xxx', 'a')
    create_file('xxx', 'b')
    create_file('xxx', 'c')

    def set_mtime(path, mtime):
        full_path = os.path.join(TESTDIR_NAME, path)
        subprocess.call(['touch', '-m', '-d', str(mtime), full_path])

    set_mtime('a', '2004-02-29  16:21:42')
    set_mtime('b', '2004-02-29  16:21:42')
    set_mtime('c', '2004-02-29  16:21:43')

    head, *data, footer = run_rmlint('--ignore-mtime')
    assert len(data) == 3
    assert footer['total_files'] == 3
    assert footer['total_lint_size'] == 6
    assert footer['duplicates'] == 2

    head, *data, footer = run_rmlint('--consider-mtime')

    assert len(data) == 2
    assert footer['total_files'] == 3
    assert footer['total_lint_size'] == 3
    assert footer['duplicates'] == 1
