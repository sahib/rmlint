from tests.utils import create_file, run_rmlint


def test_negative_with_basename(usual_setup_usual_teardown):
    create_file('xxx', 'a')
    create_file('xxx', 'b')
    _, *_, footer = run_rmlint('-b')
    assert footer['total_files'] == 2
    assert footer['total_lint_size'] == 0
    assert footer['duplicates'] == 0


def test_positive_with_basename(usual_setup_usual_teardown):
    create_file('xxx', 'a/test')
    create_file('xxx', 'b/test')
    _, *_, footer = run_rmlint('-b')
    assert footer['total_files'] == 2
    assert footer['total_lint_size'] == 3
    assert footer['duplicates'] == 1


def test_negative_without_basename(usual_setup_usual_teardown):
    create_file('xxx', 'a/test')
    create_file('xxx', 'b/test')
    _, *_, footer = run_rmlint('-B')
    assert footer['total_files'] == 2
    assert footer['total_lint_size'] == 0
    assert footer['duplicates'] == 0


def test_positive_without_basename(usual_setup_usual_teardown):
    create_file('xxx', 'a/test1')
    create_file('xxx', 'b/test2')
    _, *_, footer = run_rmlint('-B')
    assert footer['total_files'] == 2
    assert footer['total_lint_size'] == 3
    assert footer['duplicates'] == 1
