#!/usr/bin/env python3
# encoding: utf-8
from nose import with_setup
from tests.utils import *


@with_setup(usual_setup_func, usual_teardown_func)
def test_simple():
    full_path_a = create_file('x', '\t\r\"\b\f\\')
    full_path_b = create_file('x', '\"\t\n2134124')
    head, *data, footer = run_rmlint('-S a')

    assert len(data) == 2

    for i in range(2):
        with open(data[i]['path']) as f:
            assert len(f.read()) == 1

    assert os.stat(full_path_a).st_size ==  data[0]['size']
    assert os.stat(full_path_b).st_size ==  data[1]['size']
    assert footer['total_lint_size'] == 1


@with_setup(usual_setup_func, usual_teardown_func)
def test_hardlink_of():
    create_file('xxx', 'a')
    create_link('a', 'c')

    _, *data, _ = run_rmlint('-S a')
    assert len(data) == 2
    assert data[0]['type'] == 'duplicate_file'
    assert data[0]['path'].endswith('/a')
    assert data[1]['type'] == 'duplicate_file'
    assert data[1]['path'].endswith('/c')
    assert data[1]['hardlink_of'] == data[0]['id']
