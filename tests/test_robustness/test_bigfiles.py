import os
import subprocess

import pytest

from tests.utils import RMLINT_BINARY, create_file, run_rmlint

FILE_SIZE_KB = 10000
DIFFERENT_BYTES = 1
KBYTES_FROM_END = 10

LARGE_FILE_SIZE = 5 * 1024**3  # 5 GiB


def test_bigfiles():
    """test on two identical files and a third one which differs near the end"""

    create_file('x' * FILE_SIZE_KB * 1024, 'file1')
    create_file('x' * FILE_SIZE_KB * 1024, 'file2')
    create_file('x' * (FILE_SIZE_KB - KBYTES_FROM_END) * 1024 +
                'y' * DIFFERENT_BYTES +
                'x' * (KBYTES_FROM_END * 1024 - DIFFERENT_BYTES),
                'file3')

    *_, footer = run_rmlint('')
    assert footer['duplicates'] == 1


def _setup_large_file_offset():

    path_a = create_file('', 'a')
    path_b = create_file('', 'b')
    path_c = create_file('', 'c')

    os.truncate(path_a, 4 * 1024)
    if os.stat(path_a).st_blocks:
        # only really works on Linux
        pytest.skip('cannot make sparse files with truncate()')

    # allocate large sparse files
    os.truncate(path_a, LARGE_FILE_SIZE)
    os.truncate(path_b, LARGE_FILE_SIZE)
    os.truncate(path_c, LARGE_FILE_SIZE)

    # touch last byte of one file
    with open(path_a, 'r+', encoding='ascii') as f:
        f.seek(LARGE_FILE_SIZE - 1)
        f.write('x')

    return path_a, path_b, path_c


def test_hash_utility():
    path_a, path_b, path_c = _setup_large_file_offset()

    # only files 'b' and 'c' should match
    # metro is chosen because it's faster
    output = subprocess.check_output([
        RMLINT_BINARY, '--hash', '-a', 'metro',
        path_a, path_b, path_c,
    ])
    hashes = [l.split()[0] for l in output.splitlines()]
    assert hashes[0] != hashes[1]  # a != b
    assert hashes[1] == hashes[2]  # b == c
