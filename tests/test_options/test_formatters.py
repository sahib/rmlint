#!/usr/bin/env python3
# encoding: utf-8
from nose import with_setup
from tests.utils import *
import os

default_outputs = ['rmlint.json', 'rmlint.sh']


def cleanup_old_outputs(dirname=None):
    for f in default_outputs:
        p = os.path.join(dirname, f) if dirname else f
        if os.path.exists(p):
            os.remove(p)


@with_setup(usual_setup_func, usual_teardown_func)
def test_o_O():

    cwd = os.getcwd()  # not really needed but playing safe...

    for opt in '-o', '-O':

        cleanup_old_outputs()

        sh_path = os.path.join(TESTDIR_NAME, 'custom.sh')

        head, *data, footer = run_rmlint(
            '-S a {o} sh:{t}'.format(o=opt, t=sh_path),
            with_json=False)

        assert os.path.exists(sh_path)

        defaults_should_exist = (opt == '-O')
        for f in default_outputs:
            assert os.path.exists(
                os.path.join(cwd, f)) == defaults_should_exist
