from nose import with_setup
from utils import *


@with_setup(usual_setup_func, usual_teardown_func)
def test_simple():
    create_file('xxx', '.a/1')
    create_file('xxx', '.b/1')
    create_file('xxx', '.1')
    head, *data, footer = run_rmlint('--hidden')

    assert footer['duplicates'] == 2
    assert footer['ignored_folders'] == 0
    assert footer['ignored_files'] == 0
    assert footer['duplicate_sets'] == 1


@with_setup(usual_setup_func, usual_teardown_func)
def test_hidden():
    create_file('xxx', '.a/1')
    create_file('xxx', '.b/1')
    create_file('xxx', '.1')
    head, *data, footer = run_rmlint('--no-hidden')

    assert footer['duplicates'] == 0
    assert footer['ignored_folders'] == 2
    assert footer['ignored_files'] == 3
    assert footer['duplicate_sets'] == 0


@with_setup(usual_setup_func, usual_teardown_func)
def test_explicit():
    create_file('xxx', '.a/1')
    create_file('xxx', '.a/2')
    head, *data, footer = run_rmlint('--no-hidden', dir_suffix='.a')

    assert footer['duplicates'] == 1
    assert footer['ignored_folders'] == 0
    assert footer['ignored_files'] == 0
    assert footer['duplicate_sets'] == 1
