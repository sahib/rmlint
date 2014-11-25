from nose import with_setup
from .utils import *
import os


def create_bad_link(link_name):
    link_name = os.path.join(TESTDIR_NAME, link_name)
    fake_target = link_name + '.target'
    with open(fake_target, 'w') as h:
        h.write('xxx')

    try:
        os.symlink(fake_target, link_name)
    finally:
        os.remove(fake_target)


@with_setup(usual_setup_func, usual_teardown_func)
def test_basic():
    create_bad_link('imbad')
    head, *data, footer = run_rmlint('-f')

    assert data[0]['type'] == 'badlink'
    assert data[0]['path'].endswith('imbad')
