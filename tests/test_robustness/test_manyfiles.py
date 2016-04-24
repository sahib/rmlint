#!/usr/bin/env python3
# encoding: utf-8
from nose.plugins.attrib import attr
from nose import with_setup
from tests.utils import *

@attr('slow')
@with_setup(usual_setup_func, usual_teardown_func)
def test_manyfiles():

    # create heaps of identical files:
    numfiles = 1024 * 32 + 1
    for i in range(numfiles):
        create_file('xxx', 'file' + str(i).zfill(7))

    # create heaps of identical pairs:
    numpairs = 1024 * 32 + 1
    for i in range(numpairs):
        create_file(str(i), 'a' + str(i).zfill(7))
        create_file(str(i), 'b' + str(i).zfill(7))

    head, *data, footer = run_rmlint('')
    assert len(data) == numfiles + numpairs * 2
