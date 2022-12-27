#!/usr/bin/env python3
# encoding: utf-8
from tests.utils import *
import os


def create_data(len, flips=None):
    data = ['0'] * len
    for flip in flips or []:
        data[flip] = '1'

    return ''.join(data)


def test_small_diffs(usual_setup_usual_teardown):

    if use_valgrind():
        N = 32
    else:
        # Takes horribly long elsewhise
        N = 128

    create_file(create_data(len=N, flips=None), 'a')
    create_file(create_data(len=N, flips=[-1]), 'b')
    head, *data, footer = run_rmlint('-S a')

    assert len(data) == 0

    create_file(create_data(len=N, flips=[+1]), 'a')
    create_file(create_data(len=N, flips=[-1]), 'b')
    head, *data, footer = run_rmlint('-S a')

    assert len(data) == 0

    create_file(create_data(len=N, flips=[+1]), 'a')
    create_file(create_data(len=N, flips=[+1]), 'b')
    head, *data, footer = run_rmlint('-S a')

    assert len(data) == 2

    for i in range(0, N // 2):
        create_file(create_data(len=N, flips=[+i]), 'a')
        create_file(create_data(len=N, flips=[-i]), 'b')
        head, *data, footer = run_rmlint('-S a')

        if i == N - i or i == 0:
            assert len(data) == 2
        else:
            assert len(data) == 0


def test_one_byte_file_negative(usual_setup_usual_teardown):
    create_file('1', 'one')
    create_file('2', 'two')
    head, *data, footer = run_rmlint('-S a')

    assert len(data) == 0


def test_one_byte_file_positive(usual_setup_usual_teardown):
    create_file('1', 'one')
    create_file('1', 'two')
    head, *data, footer = run_rmlint('-S a')

    assert len(data) == 2

def test_two_hardlinks(usual_setup_usual_teardown):
    create_file('xxx', 'a')
    create_link('a', 'b')
    head, *data, footer = run_rmlint('-S a')

    assert len(data) == 2
    assert footer['total_lint_size'] == 0

def test_two_external_hardlinks(usual_setup_usual_teardown):
    create_file('xxx', 'a')
    create_file('xxx', 'b')
    create_dirs('sub')
    create_link('a', 'sub/a')
    create_link('a', 'sub/b')
    head, *data, footer = run_rmlint('-S a')

    assert len(data) == 4
    assert footer['total_lint_size'] == 3

    head, *data, footer = run_rmlint(
        "{t}/sub".format(t=TESTDIR_NAME),
        use_default_dir=False
    )

    # No effective lint: Removing any link will not save any disk space.
    assert len(data) == 2
    assert footer['total_lint_size'] == 0
