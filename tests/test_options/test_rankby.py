#!/usr/bin/env python3
# encoding: utf-8
import os
from tests.utils import *


def filter_part_of_directory(data):
    return [e for e in data if e['type'] != 'part_of_directory']


def test_rankby_simple(usual_setup_usual_teardown):
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


def test_rankby_dirs(usual_setup_usual_teardown):
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


def test_rankby_dir_path(usual_setup_usual_teardown):
    create_file('x', 'b/x')
    create_file('x', 'a/y')
    create_file('yyy', 'a/v')
    create_file('yyy', 'b/w')

    head, *data, foot = run_rmlint('--sort-by a -S f')
    relpaths = [os.path.relpath(p['path'], TESTDIR_NAME) for p in data]
    assert relpaths == ['a/v', 'b/w', 'a/y', 'b/x']

    head, *data, foot = run_rmlint('--sort-by S -S F')
    relpaths = [os.path.relpath(p['path'], TESTDIR_NAME) for p in data]
    assert relpaths == ['b/w', 'a/v', 'b/x', 'a/y']
