#!/usr/bin/env python3
# encoding: utf-8
from nose.plugins.attrib import attr
from nose import with_setup
from tests.utils import *
import string



FILE_SIZE = 256 * 1024 * 1024
MIDDLE = FILE_SIZE / 2

@attr('slow')
@with_setup(usual_setup_func, usual_teardown_func)
def test_1000_files():

    # this is really a test of mem limiter for paranoid hashing

    for c in string.ascii_lowercase:
        for d in string.ascii_lowercase:
            create_file(c + d, '{c}{d}1'.format(c=c, d=d),
                sparse_bytes_before = MIDDLE,
                sparse_bytes_total = FILE_SIZE)

    # make duplicates *after* all originals so that their inode numbers are
    # separated from originals
    for c in string.ascii_lowercase:
        for d in string.ascii_lowercase:
            create_file(c + d, '{c}{d}2'.format(c=c, d=d),
                sparse_bytes_before = MIDDLE,
                sparse_bytes_total = FILE_SIZE)

    *_, footer = run_rmlint('-a paranoid')
    assert footer['duplicates'] == 26 * 26
