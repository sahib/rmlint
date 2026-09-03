import os

from tests.utils import create_file, run_rmlint, get_testdir


def search_paths(*roots):
    return ' '.join(os.path.join(get_testdir(), root) for root in roots)


def dupe_paths(data):
    return {
        os.path.relpath(entry['path'], get_testdir())
        for entry in data
        if entry['type'] == 'duplicate_file' and not entry['is_original']
    }


def test_different_depth():
    create_file('xxx', 'a/path/file')
    create_file('xxx', 'b/another/path/file')

    _, *_, footer = run_rmlint(
        '--match-relative-path ' + search_paths('a', 'b'), use_default_dir=False
    )
    assert footer['total_files'] == 2
    assert footer['total_lint_size'] == 0
    assert footer['duplicates'] == 0


def test_passed_directly():
    create_file('xxx', 'a')
    create_file('xxx', 'b')

    _, *_, footer = run_rmlint(
        '--match-relative-path ' + search_paths('a', 'b'), use_default_dir=False
        )
    assert footer['total_files'] == 2
    assert footer['total_lint_size'] == 0
    assert footer['duplicates'] == 0


def test_differs_by_basename_only():
    create_file('xxx', 'one/identical/path/alpha.txt')
    create_file('xxx', 'another/one/identical/path/beta.txt')

    _, *_, footer = run_rmlint(
        '--match-relative-path ' + search_paths('one', 'another/one'),
        use_default_dir=False,
    )
    assert footer['total_files'] == 2
    assert footer['total_lint_size'] == 0
    assert footer['duplicates'] == 0

def test_combined_with_match_basename():
    create_file('xxx', 'a/path/file')
    create_file('xxx', 'b/other/file')
    create_file('xxx', 'b/path/file')

    _, *data, footer = run_rmlint(
        '-b --match-relative-path ' + search_paths('a', 'b'), use_default_dir=False
    )
    assert footer['total_files'] == 3
    assert footer['duplicates'] == 1
    assert dupe_paths(data) == {'b/path/file'}


def test_case_insensitive_ascii():
    create_file('xxx', 'a/path/File.eXt')
    create_file('xxx', 'b/PaTh/file.ext')

    _, *_, footer = run_rmlint(
        '--match-relative-path --case-insensitive ' + search_paths('a', 'b'), use_default_dir=False
    )
    assert footer['total_files'] == 2
    assert footer['total_lint_size'] == 3
    assert footer['duplicates'] == 1


def test_case_insensitive_non_ascii():
    create_file('xxx', 'a/ПÄ/file')
    create_file('xxx', 'b/пä/file')

    _, *_, footer = run_rmlint(
        '--match-relative-path --case-insensitive ' + search_paths('a', 'b'), use_default_dir=False
    )
    assert footer['total_files'] == 2
    assert footer['total_lint_size'] == 0
    assert footer['duplicates'] == 0
