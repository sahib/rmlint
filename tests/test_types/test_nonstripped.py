#!/usr/bin/env python3
import os

from tests.utils import *

SOURCE = '''
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

int main(int argc, char *argv[]) {
    puts("Hello rmlint. Why were you executing this?");
    return EXIT_SUCCESS;
}
'''


def create_binary(path, stripped=False):
    path = path + '.stripped' if stripped else path + '.nonstripped'
    full_path = os.path.join(TESTDIR_NAME, path)

    command = '{cc} -o {path} {option} -std=c99 -xc -'.format(
        cc=os.environ.get('CC', 'gcc'), path=full_path, option='-s' if stripped else '-ggdb3',
    )
    subprocess.run(command, input=SOURCE, shell=True, universal_newlines=True, check=True)


def test_negative(usual_setup_usual_teardown):
    if has_feature('nonstripped') is False:
        return

    create_file(SOURCE, 'source.c')
    create_binary('source.c', stripped=True)
    head, *data, footer = run_rmlint('-T "none +nonstripped"')
    assert footer['total_files'] == 2
    assert footer['total_lint_size'] == 0
    assert len(data) == 0


def test_positive(usual_setup_usual_teardown):
    if has_feature('nonstripped') is False:
        return

    create_file(SOURCE, 'source.c')
    create_binary('source.c', stripped=False)
    head, *data, footer = run_rmlint('-T "none +nonstripped"')
    assert footer['total_files'] == 2
    assert footer['total_lint_size'] == 0  # We cannot determine exact lint size.
    assert data[0]['type'] == 'nonstripped'


# regression test for GitHub issue #555
def test_executable_fifo(usual_setup_usual_teardown):
    if has_feature('nonstripped') is False:
        pytest.skip("needs 'nonstripped' feature")

    fifo_path = os.path.join(TESTDIR_NAME, 'fifo')
    os.mkfifo(fifo_path)
    os.chmod(fifo_path, 0o755)

    # executable FIFO should not hang rmlint
    head, *data, footer = run_rmlint('-T nonstripped', timeout=5)
    assert footer['total_files'] == 0
    assert footer['total_lint_size'] == 0
    assert not data
