from nose.plugins.attrib import attr
from nose import with_setup
from tests.utils import *

FILE_SIZE_KB = 10000
DIFFERENT_BYTES = 1
KBYTES_FROM_END = 10

@with_setup(usual_setup_func, usual_teardown_func)
def test_bigfiles():

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

