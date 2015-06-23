from nose import with_setup
from tests.utils import *
import os


def create_data(len, flips=None):
    data = ['0'] * len
    for flip in flips or []:
        data[flip] = '1'

    return ''.join(data)


@with_setup(usual_setup_func, usual_teardown_func)
def test_small_diffs():

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

        if i == N - i or i is 0:
            assert len(data) == 2
        else:
            assert len(data) == 0
