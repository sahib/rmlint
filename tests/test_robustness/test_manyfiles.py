import pytest

from tests.utils import create_file, run_rmlint


@pytest.mark.slow
def test_manyfiles(usual_setup_usual_teardown):
    # create heaps of identical files:
    numfiles = 1024 * 32 + 1
    for i in range(numfiles):
        create_file('xxx', 'file' + str(i).zfill(7))

    # create heaps of identical pairs:
    numpairs = 1024 * 32 + 1
    for i in range(numpairs):
        create_file(str(i), 'a' + str(i).zfill(7))
        create_file(str(i), 'b' + str(i).zfill(7))

    _, *data, _ = run_rmlint('')
    assert len(data) == numfiles + numpairs * 2
