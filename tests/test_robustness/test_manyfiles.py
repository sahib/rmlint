from nose import with_setup
from tests.utils import *

@with_setup(usual_setup_func, usual_teardown_func)
def test_manyfiles():
    
    # create heaps of identical files:
    numfiles=1024 * 128 + 1
    for i in range(0,numfiles):
        create_file('xxx', 'file' + str(i).zfill(7))

    # create heaps of identical pairs:
    numpairs= 1024 * 128 + 1
    for i in range(0,numpairs):
        create_file(str(i), 'a' + str(i).zfill(7))
        create_file(str(i), 'b' + str(i).zfill(7))

    head, *data, footer = run_rmlint('-vvv')
    assert len(data) == numfiles + numpairs * 2

