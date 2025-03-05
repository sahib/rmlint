#!/usr/bin/env python3
# encoding: utf-8
from tests.utils import *

def test_negative(usual_setup_usual_teardown):
    create_file('xxx', 'a.png')
    create_file('xxx', 'b.jpg')
    create_file('xxx', 'b')
    head, *data, footer = run_rmlint('-e')
    assert footer['total_files'] == 3
    assert footer['total_lint_size'] == 0
    assert footer['duplicates'] == 0


def test_positive(usual_setup_usual_teardown):
    create_file('xxx', 'a.png')
    create_file('xxx', 'b.png')
    head, *data, footer = run_rmlint('-e')
    assert footer['total_files'] == 2
    assert footer['total_lint_size'] == 3
    assert footer['duplicates'] == 1
