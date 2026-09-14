import os
import stat

from tests.utils import create_file, get_testdir, run_rmlint_once


def test_backup_skips_symlink_to_dev_null():
    """--backup must not rename special targets (issue #763)."""
    create_file('xxx', 'a')
    create_file('xxx', 'b')

    stamp = os.path.join(get_testdir(), 'stamp-link')
    os.symlink('/dev/null', stamp)

    run_rmlint_once(
        f'-S a --backup -o stamp:{stamp}',
        outputs=(),
        with_json=True,
    )

    assert os.path.islink(stamp)
    assert os.readlink(stamp) == '/dev/null'
    leftovers = [
        name for name in os.listdir(get_testdir())
        if name.startswith('stamp-link.')
    ]
    assert leftovers == []


def test_backup_skips_dev_null_itself():
    if not os.path.exists('/dev/null'):
        return
    before = os.stat('/dev/null')
    if not stat.S_ISCHR(before.st_mode):
        return

    create_file('xxx', 'a')
    create_file('xxx', 'b')

    run_rmlint_once(
        '-S a --backup -o stamp:/dev/null',
        outputs=(),
        with_json=True,
    )

    after = os.stat('/dev/null')
    assert stat.S_ISCHR(after.st_mode)
    assert after.st_rdev == before.st_rdev
