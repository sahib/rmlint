#!/usr/bin/env python3
# encoding: utf-8
from nose import with_setup
from tests.utils import *


# Regression test for directly passing broken symbolic links
# to the command line. See https://github.com/sahib/rmlint/pull/444
@with_setup(usual_setup_func, usual_teardown_func)
def test_bad_symlinks_as_direct_args():
    create_file('xxx', 'a')
    create_file('xxx', 'b')

    # Create symbolic links:
    create_link('a', 'link_a', symlink=True)
    create_link('b', 'link_b', symlink=True)

    link_a_path = os.path.join(TESTDIR_NAME, 'link_a')
    link_b_path = os.path.join(TESTDIR_NAME, 'link_b')

    # Remove original files:
    os.remove(os.path.join(TESTDIR_NAME, 'a'))
    os.remove(os.path.join(TESTDIR_NAME, 'b'))

    # Directly point rmlint to symlinks, should result
    # in directly finding them.
    head, *data, footer = run_rmlint(link_a_path, link_b_path)
    assert len(data) == 2
    assert data[0]['type'] == 'badlink'
    assert data[1]['type'] == 'badlink'

    print({data[0]['path'], data[1]['path']})
    assert {data[0]['path'], data[1]['path']} == \
            {link_a_path, link_b_path}
