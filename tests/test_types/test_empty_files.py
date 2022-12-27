#!/usr/bin/env python3
# encoding: utf-8
from tests.utils import *


def test_simple(usual_setup_usual_teardown):
    create_file('xxx', 'not_empty')
    create_file('', 'very_empty')
    head, *data, footer = run_rmlint('-T "none +ef"')

    assert footer['total_files'] == 2
    assert footer['duplicates'] == 0
    assert footer['total_lint_size'] == 0
    assert len(data) == 1
    assert data[0]['size'] == 0
