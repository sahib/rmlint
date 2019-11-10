#!/usr/bin/env python3
# encoding: utf-8
from nose import with_setup
from tests.utils import *


def create_set():
    for suffix in 'abc':
        create_file('x' * 2048, 'big' + suffix)
        create_file('x' * 1024, 'middle' + suffix)
        create_file('x' * 512, 'small' + suffix)


@with_setup(usual_setup_func, usual_teardown_func)
def test_valid():
    create_set()

    # Scalar:
    *_, footer = run_rmlint('--size 0')
    assert footer['duplicates'] == 6
    *_, footer = run_rmlint('--size 1024')
    assert footer['duplicates'] == 4
    *_, footer = run_rmlint('--size 2048')
    assert footer['duplicates'] == 2
    *_, footer = run_rmlint('--size 2049')
    assert footer['duplicates'] == 0

    # Ranges:
    *_, footer = run_rmlint('--size 1024-2048')
    assert footer['duplicates'] == 4

    *_, footer = run_rmlint('--size 0-1024')
    assert footer['duplicates'] == 4

    *_, footer = run_rmlint('--size 2048-2048')
    assert footer['duplicates'] == 2

    *_, footer = run_rmlint('--size 2K-2KB')
    assert footer['duplicates'] == 2

    *_, footer = run_rmlint('--size 2K-2KB')
    assert footer['duplicates'] == 2

    *_, footer = run_rmlint('--size 18446744073709551615-18446744073709551615')
    assert footer['duplicates'] == 0

    *_, footer = run_rmlint('--size 1-18446744073709551615')
    assert footer['duplicates'] == 6

@with_setup(usual_setup_func, usual_teardown_func)
def test_invalid():
    create_set()

    def trigger(*args):
        try:
            run_rmlint(*args)
        except subprocess.CalledProcessError:
            pass
        else:
            print(args, 'did not trigger an error exit.')
            assert False

    # Not a valid range:
    trigger('--size \-\-17')

    # max < min
    trigger('--size 10-9')

    # double min
    trigger('--size 10--10')

    # overflow by one.
    trigger('--size 0-18446744073709551616')

    # overflow by factor.
    trigger('--size 0-18446744073709551615M')

    # overflow by fraction.
    trigger('--size 0-18446744073709551615.1')



@with_setup(usual_setup_func, usual_teardown_func)
def test_replay_size():
    create_file('', 'empty1')
    create_file('', 'empty2')
    create_file('xxx', 'a/xxx')
    create_file('xxx', 'b/xxx')
    create_file('yyy', 'a/yyy')
    create_file('yyy', 'b/yyy')
    create_testdir('empty_dir')

    replay_path = '/tmp/replay.json'
    head, *data, footer = run_rmlint('-o json:{p}'.format(
        p=replay_path
    ))

    assert len(data) == 7
    assert [e["type"] for e in data] == \
           ["emptydir"] + (["emptyfile"] * 2) + (["duplicate_file"] * 4)

    head, *data, footer = run_rmlint('--replay {p} --size 1-10B'.format(
        p=replay_path
    ))

    assert [e["type"] for e in data] == \
           ["emptydir"] + (["emptyfile"] * 2) + (["duplicate_file"] * 4)
