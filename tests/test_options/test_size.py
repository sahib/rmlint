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
    trigger('--size -17')

    # max < min
    trigger('--size 10-9')

    # double min
    trigger('--size 10--10')
