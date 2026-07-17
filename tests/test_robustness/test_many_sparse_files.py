import string

import pytest

from tests.utils import create_file, run_rmlint

FILE_SIZE = 256 * 1024 * 1024
MIDDLE = FILE_SIZE // 2

@pytest.mark.slow
def test_1000_files(usual_setup_usual_teardown):
    """this is really a test of mem limiter for paranoid hashing"""
    for c in string.ascii_lowercase:
        for d in string.ascii_lowercase:
            create_file(c + d, f'{c}{d}1',
                sparse_bytes_before = MIDDLE,
                sparse_bytes_total = FILE_SIZE)

    # make duplicates *after* all originals so that their inode numbers are
    # separated from originals
    for c in string.ascii_lowercase:
        for d in string.ascii_lowercase:
            create_file(c + d, f'{c}{d}2',
                sparse_bytes_before = MIDDLE,
                sparse_bytes_total = FILE_SIZE)

    *_, footer = run_rmlint('-a paranoid')
    assert footer['duplicates'] == 26 * 26
