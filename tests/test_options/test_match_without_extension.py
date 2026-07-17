from tests.utils import create_file, run_rmlint


def test_negative(usual_setup_usual_teardown):
    create_file('xxx', 'b.png')
    create_file('xxx', 'a.png')
    create_file('xxx', 'a')
    _, *_, footer = run_rmlint('-i')
    assert footer['total_files'] == 3
    assert footer['total_lint_size'] == 0
    assert footer['duplicates'] == 0


def test_positive(usual_setup_usual_teardown):
    create_file('xxx', 'a.png')
    create_file('xxx', 'a.jpg')
    _, *_, footer = run_rmlint('-i')
    assert footer['total_files'] == 2
    assert footer['total_lint_size'] == 3
    assert footer['duplicates'] == 1
