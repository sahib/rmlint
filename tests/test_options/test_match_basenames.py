#!/usr/bin/env python3
# encoding: utf-8
from tests.utils import *

def test_negative_with_basename(usual_setup_usual_teardown):
    create_file('xxx', 'a')
    create_file('xxx', 'b')
    head, *data, footer = run_rmlint('-b')
    assert footer['total_files'] == 2
    assert footer['total_lint_size'] == 0
    assert footer['duplicates'] == 0


def test_positive_with_basename(usual_setup_usual_teardown):
    create_file('xxx', 'a/test')
    create_file('xxx', 'b/test')
    head, *data, footer = run_rmlint('-b')
    assert footer['total_files'] == 2
    assert footer['total_lint_size'] == 3
    assert footer['duplicates'] == 1


def test_negative_without_basename(usual_setup_usual_teardown):
    create_file('xxx', 'a/test')
    create_file('xxx', 'b/test')
    head, *data, footer = run_rmlint('-B')
    assert footer['total_files'] == 2
    assert footer['total_lint_size'] == 0
    assert footer['duplicates'] == 0


def test_positive_without_basename(usual_setup_usual_teardown):
    create_file('xxx', 'a/test1')
    create_file('xxx', 'b/test2')
    head, *data, footer = run_rmlint('-B')
    assert footer['total_files'] == 2
    assert footer['total_lint_size'] == 3
    assert footer['duplicates'] == 1
