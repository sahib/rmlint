#!/usr/bin/env python3
# encoding: utf-8
from tests.utils import *

def test_simple(usual_setup_usual_teardown):
    create_file('xxx', 'not_empty/a')
    create_file('', 'empty_but_with_file/a')
    create_dirs('really_empty')
    head, *data, footer = run_rmlint('-T "none +ed +ef" -S a')

    assert footer['total_files'] == 3
    assert footer['duplicates'] == 0
    assert footer['total_lint_size'] == 0
    assert len(data) == 2
    assert data[0]['size'] == 0
    assert data[0]['type'] == "emptydir"
    assert data[1]['size'] == 0
    assert data[1]['type'] == "emptyfile"


def test_deep(usual_setup_usual_teardown):
    create_dirs('1/2/3/4/5')
    create_dirs('1/2/C/D/E')
    head, *data, footer = run_rmlint('-T "none +ed"')

    assert data[0]['path'].endswith('E')
    assert data[1]['path'].endswith('D')
    assert data[2]['path'].endswith('C')
    assert data[3]['path'].endswith('5')
    assert data[4]['path'].endswith('4')
    assert data[5]['path'].endswith('3')
    assert data[6]['path'].endswith('2')
    assert data[7]['path'].endswith('1')

    create_file('', '1/2/3/showstopper')
    head, *data, footer = run_rmlint('-T "none +ed"')

    assert data[0]['path'].endswith('E')
    assert data[1]['path'].endswith('D')
    assert data[2]['path'].endswith('C')
    assert data[3]['path'].endswith('5')
    assert data[4]['path'].endswith('4')


def test_hidden(usual_setup_usual_teardown):
    create_file('xxx', 'not_empty/.hidden')
    head, *data, footer = run_rmlint('-T "none +ed"')

    assert footer['total_files'] == 0
    assert len(data) == 0

    head, *data, footer = run_rmlint('-T "none +ed" --hidden')
    assert footer['total_files'] == 1
    assert len(data) == 0
