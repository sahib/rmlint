import os
from datetime import datetime as dt, timedelta, UTC

from tests.utils import create_file, get_testdir, run_rmlint

_EPOCH = dt(1970, 1, 1, tzinfo=UTC)
_US = timedelta(microseconds=1)


def t(second, microsecond=0) -> dt:
    return dt(2004, 2, 29, 16, 21, second, microsecond, tzinfo=UTC)


def set_mtime(path: str, mtime: dt):
    ns = (mtime - _EPOCH) // _US * 1000
    os.utime(os.path.join(get_testdir(), path), ns=(ns, ns))


def test_consider_mtime():
    create_file('xxx', 'a')
    create_file('xxx', 'b')
    create_file('xxx', 'c')
    create_file('xxx', 'd')

    set_mtime('a', t(42))
    set_mtime('b', t(42))
    set_mtime('c', t(44))
    set_mtime('d', t(45))

    _, *data, footer = run_rmlint('--mtime-window=-1')
    assert len(data) == 4
    assert footer['total_files'] == 4
    assert footer['total_lint_size'] == 9
    assert footer['duplicates'] == 3
    assert footer['duplicate_sets'] == 1

    _, *data, footer = run_rmlint('--mtime-window=0')
    assert len(data) == 2
    assert footer['total_files'] == 4
    assert footer['total_lint_size'] == 3
    assert footer['duplicates'] == 1
    assert footer['duplicate_sets'] == 1

    _, *data, footer = run_rmlint('--mtime-window=+1')
    assert len(data) == 4
    assert footer['total_files'] == 4
    assert footer['total_lint_size'] == 6  # two originals.
    assert footer['duplicates'] == 2
    assert footer['duplicate_sets'] == 2

    _, *data, footer = run_rmlint('--mtime-window=+2')
    assert len(data) == 4   # '2' also chains up to d from c.
    assert footer['total_files'] == 4
    assert footer['total_lint_size'] == 9
    assert footer['duplicates'] == 3
    assert footer['duplicate_sets'] == 1


def test_consider_mtime_subsecond():
    create_file('xxx', 'a')
    create_file('xxx', 'b')

    set_mtime('a', t(42))
    set_mtime('b', t(43, 990_000))

    _, *data, _ = run_rmlint('--mtime-window=1.9')
    assert len(data) == 0

    _, *data, _ = run_rmlint('--mtime-window=2.0')
    assert len(data) == 2

    set_mtime('a', t(42))
    set_mtime('b', t(42, 990_000))

    _, *data, _ = run_rmlint('--mtime-window=0')
    assert len(data) == 0

def test_consider_mtime_fail_by_association():
    create_file('xxx', 'a')
    create_file('yyy', 'b')
    create_file('xxx', 'c')

    set_mtime('a', t(42))
    set_mtime('b', t(44))
    set_mtime('c', t(46))

    _, *data, footer = run_rmlint('--mtime-window=3')

    assert len(data) == 0
    assert footer['total_files'] == 3
    assert footer['total_lint_size'] == 0
    assert footer['duplicates'] == 0
    assert footer['duplicate_sets'] == 0

def test_mtime_and_unmatched_basenames():
    create_file('xxx', 'dir1/a')
    create_file('xxx', 'dir1/c')
    create_file('xxx', 'dir2/a')

    create_file('yyy', 'dir1/b')
    create_file('yyy', 'dir2/b')
    create_file('yyy', 'dir2/c')

    set_mtime('dir1/a', t(42))
    set_mtime('dir1/c', t(44))
    set_mtime('dir2/a', t(48))

    set_mtime('dir1/b', t(46))
    set_mtime('dir2/b', t(48))
    set_mtime('dir2/c', t(50))

    _, *data, footer = run_rmlint('--mtime-window=3 --unmatched-basename -S m')

    assert len(data) == 2
    assert footer['total_files'] == 6
    assert footer['total_lint_size'] == 3
    assert footer['duplicates'] == 1
    assert footer['duplicate_sets'] == 1
