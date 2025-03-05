#!/usr/bin/env python3
import os
import subprocess

from tests.utils import *

FILE_SIZE_KB = 10000
DIFFERENT_BYTES = 1
KBYTES_FROM_END = 10

LARGE_FILE_SIZE = 5 * 1024**3  # 5 GiB


def test_bigfiles(usual_setup_usual_teardown):

    num_bytes = int(FILE_SIZE_KB * 1024)
    # create two identical files and a third one which differs near the end:
    create_file('x' * int(FILE_SIZE_KB * 1024), 'file1')

    create_file('x' * int(FILE_SIZE_KB * 1024), 'file2')

    create_file('x' * int((FILE_SIZE_KB - KBYTES_FROM_END )* 1024) +
                'y' * int(DIFFERENT_BYTES) +
                'x' * int(KBYTES_FROM_END * 1024 - DIFFERENT_BYTES),
                'file3')

    *_, footer = run_rmlint('')
    assert footer['duplicates'] == 1


def _setup_large_file_offset():
    if not has_feature('bigfiles'):
        raise_skiptest('rmlint built without large file support')

    path_a = create_file('', 'a')
    path_b = create_file('', 'b')
    path_c = create_file('', 'c')

    os.truncate(path_a, 4 * 1024)
    if os.stat(path_a).st_blocks:
        # only really works on Linux
        raise_skiptest('cannot make sparse files with truncate()')

    # allocate large sparse files
    os.truncate(path_a, LARGE_FILE_SIZE)
    os.truncate(path_b, LARGE_FILE_SIZE)
    os.truncate(path_c, LARGE_FILE_SIZE)

    # touch last byte of one file
    with open(path_a, 'r+') as f:
        f.seek(LARGE_FILE_SIZE - 1)
        f.write('x')

    return path_a, path_b, path_c


def test_hash_utility(usual_setup_usual_teardown):
    path_a, path_b, path_c = _setup_large_file_offset()

    # only files 'b' and 'c' should match
    # metro is chosen because it's faster
    output = subprocess.check_output([
        *'./rmlint --hash -a metro'.split(),
        path_a, path_b, path_c,
    ])
    hashes = [l.split()[0] for l in output.splitlines()]
    assert hashes[0] != hashes[1]  # a != b
    assert hashes[1] == hashes[2]  # b == c
