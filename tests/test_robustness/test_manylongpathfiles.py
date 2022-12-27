#!/usr/bin/env python3
# encoding: utf-8
import pytest
from tests.utils import *

@pytest.mark.slow
def test_manylongpathfiles(usual_setup_usual_teardown):

	#create ~1000 character path, 4 dirs deep
    longpath = ("long" * (1000//4//4) + "/") * 4
    create_dirs(longpath)

    # create heaps of identical files:
    numfiles = 1024 * 32 + 1
    for i in range(numfiles):
        create_file('xxx', longpath + 'file' + str(i).zfill(7))

    # create heaps of identical pairs:
    numpairs = 1024 * 32 + 1
    for i in range(numpairs):
        create_file(str(i), longpath + 'a' + str(i).zfill(7))
        create_file(str(i), longpath + 'b' + str(i).zfill(7))

    head, *data, footer = run_rmlint('')
    assert len(data) == numfiles + numpairs * 2
