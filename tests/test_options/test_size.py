import os
import subprocess
from pathlib import Path

import pytest

from tests.utils import create_dirs, create_file, create_testdir, get_testdir, run_rmlint


def create_set():
    for suffix in 'abc':
        create_file('x' * 2048, 'big' + suffix)
        create_file('x' * 1024, 'middle' + suffix)
        create_file('x' * 512, 'small' + suffix)


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

    *_, footer = run_rmlint('--size 1-18446744073709549K')
    assert footer['duplicates'] == 6


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
    trigger('--size --17')

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



def test_replay_size():
    create_file('', 'empty1')
    create_file('', 'empty2')
    create_file('xxx', 'a/xxx')
    create_file('xxx', 'b/xxx')
    create_file('yyy', 'a/yyy')
    create_file('yyy', 'b/yyy')
    create_testdir('empty_dir')

    replay_path = os.path.join(get_testdir(), 'replay.json')
    _, *data, _ = run_rmlint(f'-o json:{replay_path}')

    assert len(data) == 7
    assert [e["type"] for e in data] == \
           ["emptydir"] + (["emptyfile"] * 2) + (["duplicate_file"] * 4)

    _, *data, _ = run_rmlint(f'--replay {replay_path} --size 1-10B')

    assert [e["type"] for e in data] == \
           ["emptydir"] + (["emptyfile"] * 2) + (["duplicate_file"] * 4)


def test_directory_size_limit_on_tmpfs():
    def write_file(directory, name, data):
        Path(directory, name).write_text(data, encoding="utf-8")

    included = create_dirs('included')
    skipped = create_dirs('skipped')

    # XXX: directories st_size varies by filesystems, pick a number high enough.
    for index in range(64):
        write_file(get_testdir(), 'root-filler-{:03d}'.format(index), '')
        write_file(included, 'filler-{:03d}'.format(index), '')

    write_file(included, 'duplicate-a', 'x' * 1024)
    write_file(included, 'duplicate-b', 'x' * 1024)
    write_file(skipped, 'duplicate-a', 'x' * 1024)
    write_file(skipped, 'duplicate-b', 'x' * 1024)

    if (os.stat(get_testdir()).st_size < 1024
        or os.stat(included).st_size < 1024
        or os.stat(skipped).st_size >= 1024):
        pytest.skip("incompatible filesystem")

    *_, footer = run_rmlint('-T df --size 1K,d')
    assert footer['duplicates'] == 1
