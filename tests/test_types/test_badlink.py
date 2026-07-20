import os

from tests.utils import TESTDIR_NAME, run_rmlint


def create_bad_link(link_name):
    link_name = os.path.join(TESTDIR_NAME, link_name)
    fake_target = link_name + '.target'
    with open(fake_target, 'w', encoding='ascii') as h:
        h.write('xxx')

    try:
        os.symlink(fake_target, link_name)
    finally:
        os.remove(fake_target)


def test_basic():
    create_bad_link('imbad')

    for option in ('-f', '-F', '--see-symlinks'):
        _, *data, _ = run_rmlint(option)

        assert len(data) == 1
        assert data[0]['type'] == 'badlink'
        assert data[0]['path'].endswith('imbad')
