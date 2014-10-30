from nose import with_setup
from utils import *


@with_setup(usual_setup_func, usual_teardown_func)
def test_negative():
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
