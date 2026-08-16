import os

import pytest

from tests.utils import create_dirs, create_file, get_testdir, run_rmlint


@pytest.mark.slow
def test_manylongpathfiles():
    path_max = os.pathconf(get_testdir(), "PC_PATH_MAX")
    path_max = path_max if 0 < path_max <= 1024 else 1024
    prefix = os.path.abspath(get_testdir()) + os.sep
    budget = path_max - len(prefix) - 12

    # four equally-sized path components, up to min(1024, PATH_MAX)
    component_len = (budget - 4) // 4
    component = "l" * component_len
    longpath = (component + "/") * 4

    create_dirs(longpath)

    numfiles = 1024 * 32 + 1
    for i in range(numfiles):
        create_file("xxx", longpath + f"file{i:07d}")

    numpairs = 1024 * 32 + 1
    for i in range(numpairs):
        create_file(str(i), longpath + f"a{i:07d}")
        create_file(str(i), longpath + f"b{i:07d}")

    _, *data, _ = run_rmlint("")
    assert len(data) == numfiles + numpairs * 2
