#!/usr/bin/env python3
# encoding: utf-8
from tests.utils import *

import csv

def csv_string_to_data(csv_dump):
    data = list(csv.reader(csv_dump.splitlines()))
    return data[1:]


def test_simple(usual_setup_usual_teardown):
    create_file('1234', 'a')
    create_file('1234', 'b')
    create_file('1234', 'stupid\'file,name')
    head, *data, footer, csv = run_rmlint('-S a', outputs=['csv'])

    (type_1, path_1, size_1, cksum_1),  \
    (type_2, path_2, size_2, cksum_2),  \
    (type_3, path_3, size_3, cksum_3) = \
        csv_string_to_data(csv)

    assert int(size_1) == 4
    assert int(size_2) == 4
    assert int(size_3) == 4

    assert path_1.endswith('/a')
    assert path_2.endswith('/b')
    assert path_3.endswith('/stupid\'file,name')

    assert type_1 == 'duplicate_file'
    assert type_2 == 'duplicate_file'
    assert type_3 == 'duplicate_file'

    assert cksum_1 != '0' * 32
    assert cksum_2 != '0' * 32
    assert cksum_3 != '0' * 32
