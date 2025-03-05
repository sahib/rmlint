#!/usr/bin/env python3
# encoding: utf-8
from tests.utils import *

import subprocess

import pytest


def _check_interpreter(interpreter):
    try:
        subprocess.call([interpreter, "-c", "1 + 1"])
        return True
    except (subprocess.CalledProcessError, FileNotFoundError):
        return False


@pytest.mark.parametrize("interpreter", ["python2", "python3"])
def test_paranoia(usual_setup_usual_teardown, interpreter):
    if not _check_interpreter(interpreter):
        pytest.skip(
            "Interpreter {} does not seem to be working, skipping test".format(
                interpreter
            )
        )

    create_file('xxx', 'a')
    create_file('xxx', 'b')
    create_file('xxx', 'c')
    create_file('xxx', 'd')
    create_link('a', 'hardlink_a', symlink=False)

    head, *data, footer = run_rmlint(
        '-S a -o py:{t}/rmlint.py'.format(t=TESTDIR_NAME)
    )

    assert footer['duplicate_sets'] == 1
    assert footer['total_lint_size'] == 9
    assert footer['total_files'] == 5 # 1 is ignored as own output
    assert footer['duplicates'] == 4

    with open(os.path.join(TESTDIR_NAME, 'b'), 'w') as handle:
        handle.write('yyy')

    with open(os.path.join(TESTDIR_NAME, 'c'), 'w') as handle:
        handle.write('xxxx')

    text = subprocess.check_output([
        interpreter,
        os.path.join(TESTDIR_NAME, 'rmlint.py'),
        '-d',
        '-p'
    ])
    text = text.decode('utf-8')

    # subprocess.call('ls  -l ' + TESTDIR_NAME, shell=True)
    os.remove(os.path.join(TESTDIR_NAME, "rmlint.py"))
    head, *data, footer = run_rmlint(
        '-S a -o py:{t}/rmlint.py'.format(t=TESTDIR_NAME)
    )

    assert footer['duplicate_sets'] == 1
    assert footer['total_lint_size'] == 0
    assert footer['total_files'] == 4 # + 1
    assert footer['duplicates'] == 1

    assert 'Content differs' in text
    assert 'Size differs' in text
    assert 'Same inode' in text

    text = subprocess.check_output([
        interpreter,
        os.path.join(TESTDIR_NAME, 'rmlint.py'),
        '-d',
        '-p'
    ])
    os.remove(os.path.join(TESTDIR_NAME, "rmlint.py"))
    head, *data, footer = run_rmlint(
        '-S a -o py:{t}/rmlint.py'.format(t=TESTDIR_NAME)
    )

    # Nothing should change.
    assert footer['duplicate_sets'] == 1
    assert footer['total_lint_size'] == 0
    assert footer['total_files'] == 4 # + 1
    assert footer['duplicates'] == 1
