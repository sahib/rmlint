#!/usr/bin/env python3
# encoding: utf-8
from tests.utils import *


def test_simple(usual_setup_usual_teardown):
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
