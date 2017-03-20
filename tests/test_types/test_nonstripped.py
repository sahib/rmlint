#!/usr/bin/env python3
# encoding: utf-8
from nose import with_setup
from tests.utils import *
import os


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

    command = 'echo \'{src}\' | cc -o {path} {option} -std=c99 -xc -'.format(
        src=SOURCE, path=full_path, option=('-s' if stripped else '-ggdb3')
    )
    subprocess.call(command, shell=True)


@with_setup(usual_setup_func, usual_teardown_func)
def test_negative():
    if has_feature('nonstripped') is False:
        return

    create_file(SOURCE, 'source.c')
    create_binary('source.c', stripped=True)
    head, *data, footer = run_rmlint('-T "none +nonstripped"')
    assert footer['total_files'] == 2
    assert footer['total_lint_size'] == 0
    assert len(data) == 0


@with_setup(usual_setup_func, usual_teardown_func)
def test_positive():
    if has_feature('nonstripped') is False:
        return

    create_file(SOURCE, 'source.c')
    create_binary('source.c', stripped=False)
    head, *data, footer = run_rmlint('-T "none +nonstripped"')
    assert footer['total_files'] == 2
    assert footer['total_lint_size'] == 0  # We cannot determine exact lint size.
    assert data[0]['type'] == 'nonstripped'
