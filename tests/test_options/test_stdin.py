#!/usr/bin/env python3
import json
import os
import pytest

from tests.utils import *


def test_stdin_read(usual_setup_usual_teardown):
    path_a = create_file('1234', 'a') + '\n'
    path_b = create_file('1234', 'b') + '\n'
    path_c = create_file('1234', '.hidden') + '\n'

    subdir = 'look-in-here'
    create_file('1234', subdir + '/c')
    subdir_path = os.path.join(TESTDIR_NAME, subdir)

    proc = subprocess.Popen(
        ['./rmlint', '-', subdir_path, '-o', 'json', '-S', 'a', '--hidden'],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE
    )
    data, _ = proc.communicate((path_a + path_b + path_c).encode('utf-8'))
    head, *data, footer = json.loads(data.decode('utf-8'))

    assert data[0]['path'].endswith('.hidden')
    assert data[1]['path'].endswith('a')
    assert data[2]['path'].endswith('b')
    assert data[3]['path'].endswith('c')
    assert footer['total_lint_size'] == 12

def test_stdin_read_newlines(usual_setup_usual_teardown):
    path_a = create_file('1234', 'a') + '\0'
    path_b = create_file('1234', 'name\nwith\nnewlines') + '\0'
    path_c = create_file('1234', '.hidden') + '\0'

    subdir = 'look-in-here'
    create_file('1234', subdir + '/c')
    subdir_path = os.path.join(TESTDIR_NAME, subdir)

    proc = subprocess.Popen(
        ['./rmlint', '-0', subdir_path, '-o', 'json', '-S', 'a', '--hidden'],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE
    )
    data, _ = proc.communicate((path_a + path_b + path_c).encode('utf-8'))
    head, *data, footer = json.loads(data.decode('utf-8'))

    assert data[0]['path'].endswith('.hidden')
    assert data[1]['path'].endswith('a')
    assert data[2]['path'].endswith('c')
    assert data[3]['path'].endswith('newlines')
    assert footer['total_lint_size'] == 12

def test_path_starting_with_dash(usual_setup_usual_teardown):
    subdir = '-look-in-here'
    create_file('1234', subdir + '/a')
    create_file('1234', subdir + '/b')

    cwd = os.getcwd()

    try:
        os.chdir(TESTDIR_NAME)
        proc = subprocess.Popen(
            [cwd + '/rmlint', '-o', 'json', '-S', 'a', '--', subdir],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE
        )
        data, _ = proc.communicate("")
        head, *data, footer = json.loads(data.decode('utf-8'))
    finally:
        os.chdir(cwd)

    assert data[0]['path'].endswith('a')
    assert data[1]['path'].endswith('b')
    assert footer['total_lint_size'] == 4


# Regression test for https://github.com/sahib/rmlint/issues/400
# Do not search in current directory when piped empty input.
@pytest.mark.parametrize("stdin_opt", ("-", "-0"))
def test_stdin_empty(usual_setup_usual_teardown, stdin_opt):
    create_file('1234', 'a')
    create_file('1234', 'b')

    cwd = os.getcwd()

    try:
        os.chdir(TESTDIR_NAME)
        proc = subprocess.Popen(
            [cwd + '/rmlint', stdin_opt, '-o', 'json'],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE
        )
        data, _ = proc.communicate("")
        head, *data, footer = json.loads(data.decode('utf-8'))
    finally:
        os.chdir(cwd)

    assert len(data) == 0
