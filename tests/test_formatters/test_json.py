from nose import with_setup
from tests.utils import *


@with_setup(usual_setup_func, usual_teardown_func)
def test_simple():
    create_file('x', '\t\r\"\b\f\\')
    create_file('x', '\"\t\n2134124')
    head, *data, footer = run_rmlint('-S a')

    assert len(data) == 2

    for i in range(2):
        with open(data[i]['path']) as f:
            assert len(f.read()) == 1
