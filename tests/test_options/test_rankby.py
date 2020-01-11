#!/usr/bin/env python3
# encoding: utf-8
import os
from nose import with_setup
from tests.utils import *


def filter_part_of_directory(data):
    return [e for e in data if e['type'] != 'part_of_directory']


@with_setup(usual_setup_func, usual_teardown_func)
def test_rankby_simple():
    create_file('x', 'ax')
    create_file('x', 'ay')
    create_file('yyy', 'bx')
    create_file('yyy', 'by')

    head, *data, foot = run_rmlint('--sort-by a -S a')
    paths = [os.path.basename(p['path']) for p in data]
    assert paths == ['ax', 'ay', 'bx', 'by']

    head, *data, foot = run_rmlint('--sort-by S -S A')
    paths = [os.path.basename(p['path']) for p in data]
    assert paths == ['by', 'bx', 'ay', 'ax']


@with_setup(usual_setup_func, usual_teardown_func)
def test_rankby_dirs():
    create_file('x', 'ax')
    create_file('x', 'ay')
    create_file('yyy', 'b/x')
    create_file('yyy', 'b/y')
    create_file('yyy', 'c/x')
    create_file('yyy', 'c/y')
    create_file('x' * 64, 'dx')
    create_file('x' * 64, 'dy')

    head, *data, foot = run_rmlint('--sort-by s -S a -D')
    data = filter_part_of_directory(data)
    paths = [os.path.basename(p['path']) for p in data]
    assert paths == ['b', 'c', 'ax', 'ay', 'x', 'y', 'dx', 'dy']
