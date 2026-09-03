from tests.utils import create_file, get_testdir, run_rmlint


def test_matched():
    create_file('xxx', 'a/path/file')
    create_file('xxx', 'b/another/path/file')

    _, *_, footer = run_rmlint(
        '--match-dirname a b', use_default_dir=False)

    assert footer['total_files'] == 2
    assert footer['duplicates'] == 1


def test_not_matched():
    create_file('xxx', 'a/path/file')
    create_file('xxx', 'b/path/another/file')

    _, *_, footer = run_rmlint(
        '--match-dirname a b', use_default_dir=False)

    assert footer['total_files'] == 2
    assert footer['duplicates'] == 0
