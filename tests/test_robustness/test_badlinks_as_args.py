import os

from tests.utils import create_file, create_link, get_testdir, run_rmlint


def test_bad_symlinks_as_direct_args():
    """
    Regression test for directly passing broken symbolic links
    to the command line. See https://github.com/sahib/rmlint/pull/444
    """
    create_file('xxx', 'a')
    create_file('xxx', 'b')

    # Create symbolic links:
    create_link('a', 'link_a', symlink=True)
    create_link('b', 'link_b', symlink=True)

    link_a_path = os.path.join(get_testdir(), 'link_a')
    link_b_path = os.path.join(get_testdir(), 'link_b')

    # Remove original files:
    os.remove(os.path.join(get_testdir(), 'a'))
    os.remove(os.path.join(get_testdir(), 'b'))

    # Directly point rmlint to symlinks, should result
    # in directly finding them.
    _, *data, _ = run_rmlint(link_a_path, link_b_path)
    assert len(data) == 2
    assert data[0]['type'] == 'badlink'
    assert data[1]['type'] == 'badlink'

    assert {data[0]['path'], data[1]['path']} == \
            {link_a_path, link_b_path}
