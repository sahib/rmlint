from tests.utils import create_file, run_rmlint


def test_negative():
    create_file('xxx', 'a.png')
    create_file('xxx', 'b.jpg')
    create_file('xxx', 'b')
    _, *_, footer = run_rmlint('-e')
    assert footer['total_files'] == 3
    assert footer['total_lint_size'] == 0
    assert footer['duplicates'] == 0


def test_positive():
    create_file('xxx', 'a.png')
    create_file('xxx', 'b.png')
    _, *_, footer = run_rmlint('-e')
    assert footer['total_files'] == 2
    assert footer['total_lint_size'] == 3
    assert footer['duplicates'] == 1
