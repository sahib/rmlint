#!/usr/bin/env python3
# encoding: utf-8
from nose import with_setup
from tests.utils import *

@with_setup(usual_setup_func, usual_teardown_func)
def test_negative_with_basename():
    create_file('xxx', 'a')
    create_file('xxx', 'b')
    head, *data, footer = run_rmlint('-b')
    assert footer['total_files'] == 2
    assert footer['total_lint_size'] == 0
    assert footer['duplicates'] == 0


@with_setup(usual_setup_func, usual_teardown_func)
def test_positive_with_basename():
    create_file('xxx', 'a/test')
    create_file('xxx', 'b/test')
    head, *data, footer = run_rmlint('-b')
    assert footer['total_files'] == 2
    assert footer['total_lint_size'] == 3
    assert footer['duplicates'] == 1


@with_setup(usual_setup_func, usual_teardown_func)
def test_negative_without_basename():
    create_file('xxx', 'a/test')
    create_file('xxx', 'b/test')
    head, *data, footer = run_rmlint('-B')
    assert footer['total_files'] == 2
    assert footer['total_lint_size'] == 0
    assert footer['duplicates'] == 0


@with_setup(usual_setup_func, usual_teardown_func)
def test_positive_without_basename():
    create_file('xxx', 'a/test1')
    create_file('xxx', 'b/test2')
    head, *data, footer = run_rmlint('-B')
    assert footer['total_files'] == 2
    assert footer['total_lint_size'] == 3
    assert footer['duplicates'] == 1
