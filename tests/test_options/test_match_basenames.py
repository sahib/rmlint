import os

from tests.utils import create_file, get_testdir, run_rmlint


def test_negative_with_basename():
    create_file('xxx', 'a')
    create_file('xxx', 'b')
    _, *_, footer = run_rmlint('-b')
    assert footer['total_files'] == 2
    assert footer['total_lint_size'] == 0
    assert footer['duplicates'] == 0


def test_positive_with_basename():
    create_file('xxx', 'a/test')
    create_file('xxx', 'b/test')
    _, *_, footer = run_rmlint('-b')
    assert footer['total_files'] == 2
    assert footer['total_lint_size'] == 3
    assert footer['duplicates'] == 1


def test_negative_without_basename():
    create_file('xxx', 'a/test')
    create_file('xxx', 'b/test')
    _, *_, footer = run_rmlint('-B')
    assert footer['total_files'] == 2
    assert footer['total_lint_size'] == 0
    assert footer['duplicates'] == 0


def test_positive_without_basename():
    create_file('xxx', 'a/test1')
    create_file('xxx', 'b/test2')
    _, *_, footer = run_rmlint('-B')
    assert footer['total_files'] == 2
    assert footer['total_lint_size'] == 3
    assert footer['duplicates'] == 1


TOTAL_FILES = 10
def test_split():
    """Identical files split into basenames a.txt and b.txt should give two equal groups."""

    roots = []
    for idx in range(TOTAL_FILES):
        create_file('xxx', f'{idx}/{'a.ext' if idx % 2 == 0 else 'b.ext'}')
        roots.append(os.path.join(get_testdir(), str(idx)))

    _, *data, footer = run_rmlint(
        '-b ' + ' '.join(roots), use_default_dir=False
    )

    assert footer['total_files'] == TOTAL_FILES

    groups = []
    for entry in data:
        if entry['type'] != 'duplicate_file':
            continue
        if entry['is_original']:
            groups.append(0)
        groups[-1] += 1

    assert groups == [TOTAL_FILES // 2] * 2
    assert footer['duplicates'] == TOTAL_FILES - 2


def test_one_group():
    """If the basenames are all the same, we should get one group."""

    roots = []
    for idx in range(TOTAL_FILES):
        create_file('xxx', f'{idx}/a.ext')
        roots.append(os.path.join(get_testdir(), str(idx)))

    _, *_, footer = run_rmlint(
        '-b ' + ' '.join(roots), use_default_dir=False)

    assert footer['total_files'] == TOTAL_FILES
    assert footer['duplicates'] == TOTAL_FILES - 1
