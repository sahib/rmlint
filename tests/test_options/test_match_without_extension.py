#!/usr/bin/env python3
# encoding: utf-8
from nose import with_setup
from tests.utils import *

@with_setup(usual_setup_func, usual_teardown_func)
def test_negative():
    create_file('xxx', 'b.png')
    create_file('xxx', 'a.png')
    create_file('xxx', 'a')
    head, *data, footer = run_rmlint('-i')
    assert footer['total_files'] == 3
    assert footer['total_lint_size'] == 0
    assert footer['duplicates'] == 0


@with_setup(usual_setup_func, usual_teardown_func)
def test_positive():
    create_file('xxx', 'a.png')
    create_file('xxx', 'a.jpg')
    head, *data, footer = run_rmlint('-i')
    assert footer['total_files'] == 2
    assert footer['total_lint_size'] == 3
    assert footer['duplicates'] == 1
