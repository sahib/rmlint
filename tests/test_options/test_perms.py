#!/usr/bin/env python3
# encoding: utf-8
from tests.utils import *

import stat

from itertools import chain, combinations


def create_file_with_perms(content, path, permissions):
    perms = 0
    if 'r' in permissions:
        perms |= stat.S_IRUSR

    if 'w' in permissions:
        perms |= stat.S_IWUSR

    if 'x' in permissions:
        perms |= stat.S_IXUSR

    create_file(content, path)
    os.chmod(os.path.join(TESTDIR_NAME, path), perms)


def test_combinations(usual_setup_usual_teardown):
    # This test does not work when run as root.
    # root can read the files anyways.
    if runs_as_root():
        return

    # Generate all combinations of 'rwx' in a fun way:
    rwx = set(
        chain(
            "rwx",
            (''.join(s) for s in combinations('rwx', 2)),
            ["rwx"]
        )
    )

    create_file_with_perms('xxx', 'none', '')
    for perm in rwx:
        create_file_with_perms('xxx', perm, perm)

    files_created = len(rwx) + 1

    head, *data, footer = run_rmlint('')
    assert footer['duplicate_sets'] == 1
    assert footer['ignored_files'] == 0
    assert footer['total_files'] == files_created
    assert footer['duplicates'] == 3

    for perm_opt, dupes in zip('rwx', (3, 1, 1)):
        head, *data, footer = run_rmlint('--perms ' + perm_opt)
        assert footer['duplicate_sets'] == 1
        assert footer['ignored_files'] == 4
        assert footer['total_files'] == files_created - 4
        assert footer['duplicates'] == dupes

    head, *data, footer = run_rmlint('--perms rwx')
    assert footer['duplicate_sets'] == 0
    assert footer['ignored_files'] == 7
    assert footer['total_files'] == 1
    assert footer['duplicates'] == 0

    for perm_opt, dupes in zip(["rw", "rx", "wx"], (1, 1, 0)):
        head, *data, footer = run_rmlint('--perms ' + perm_opt)
        assert footer['duplicate_sets'] == dupes
        assert footer['ignored_files'] == 6
        assert footer['total_files'] == files_created - 6
        assert footer['duplicates'] == dupes
