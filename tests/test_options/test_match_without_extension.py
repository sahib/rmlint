from tests.utils import create_file, run_rmlint


def test_negative():
    create_file('xxx', 'b.png')
    create_file('xxx', 'a.png')
    create_file('xxx', 'a')
    _, *_, footer = run_rmlint('-i')
    assert footer['total_files'] == 3
    assert footer['total_lint_size'] == 0
    assert footer['duplicates'] == 0


def test_positive():
    create_file('xxx', 'a.png')
    create_file('xxx', 'a.jpg')
    _, *_, footer = run_rmlint('-i')
    assert footer['total_files'] == 2
    assert footer['total_lint_size'] == 3
    assert footer['duplicates'] == 1


def test_extensionless():
    """Regression test for rank.c patch in #779.
    Test that two extensionless filenames that begin
    with the same string are NOT grouped.
    """
    create_file('xxx', 'abc')
    create_file('xxx', 'abcd')
    _, *_, footer = run_rmlint('-i')
    assert footer['total_files'] == 2
    assert footer['total_lint_size'] == 0
    assert footer['duplicates'] == 0


def test_extensionless_grouped():
    """Regression test for rank.c patch in #779.
    Test that two files without extension are still
    grouped correctly.
    """
    create_file('xxx', 'one/abc')
    create_file('xxx', 'two/abc')
    _, *_, footer = run_rmlint('-i')
    assert footer['total_files'] == 2
    assert footer['total_lint_size'] == 3
    assert footer['duplicates'] == 1
