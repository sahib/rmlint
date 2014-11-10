from nose import with_setup
from utils import *


@with_setup(usual_setup_func, usual_teardown_func)
def test_simple():
    create_file('xxx', '1/a')
    create_file('xxx', '2/a')
    create_file('xxx', 'a')
    head, *data, footer = run_rmlint('-D --sortcriteria A')

    assert 2 == sum(find['type'] == 'duplicate_dir' for find in data)
    assert 1 == sum(find['type'] == 'duplicate_file' for find in data)
    assert data[0]['size'] == 3

    # -S A should sort in reverse lexigraphic order.
    assert data[0]['is_original']
    assert not data[1]['is_original']
    assert data[0]['path'].endswith('2')
    assert data[1]['path'].endswith('1')


@with_setup(usual_setup_func, usual_teardown_func)
def test_diff():
    create_file('xxx', '1/a')
    create_file('xxx', '2/a')
    create_file('xxx', '3/a')
    create_file('yyy', '3/b')
    head, *data, footer = run_rmlint('-D --sortcriteria A')

    assert 2 == sum(find['type'] == 'duplicate_dir' for find in data)
    assert data[0]['size'] == 3

    # -S A should sort in reverse lexigraphic order.
    assert data[0]['is_original']
    assert not data[1]['is_original']
    assert data[0]['path'].endswith('2')
    assert data[1]['path'].endswith('1')


@with_setup(usual_setup_func, usual_teardown_func)
def test_same_but_not_dupe():
    create_file('xxx', '1/a')
    create_file('xxx', '2/a')
    create_file('xxx', '2/b')
    head, *data, footer = run_rmlint('-D --sortcriteria A')

    # No duplicate dirs, but 3 duplicate files should be found.
    assert 0 == sum(find['type'] == 'duplicate_dir' for find in data)
    assert 3 == sum(find['type'] == 'duplicate_file' for find in data)

@with_setup(usual_setup_func, usual_teardown_func)
def test_hardlinks():
    create_file('xxx', '1/a')
    create_link('1/a', '1/link1')
    create_link('1/a', '1/link2')
    create_file('xxx', '2/a')
    create_link('2/a', '2/link1')
    create_link('2/a', '2/link2')
    head, *data, footer = run_rmlint('-D -l -S a')

    assert data[0]['type'] == 'duplicate_dir'
    assert data[0]['path'].endswith('1')
    assert data[1]['type'] == 'duplicate_dir'
    assert data[1]['path'].endswith('2')

    head, *data, footer = run_rmlint('-D -S A')
    assert data[0]['type'] == 'duplicate_file'
    assert data[0]['path'].endswith('a')
    assert data[1]['type'] == 'duplicate_file'
    assert data[1]['path'].endswith('a')

